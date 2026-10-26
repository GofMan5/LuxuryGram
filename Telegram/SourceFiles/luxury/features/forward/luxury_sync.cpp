// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/features/forward/luxury_sync.h"

#include "apiwrap.h"
#include "api/api_common.h"
#include "api/api_sending.h"
#include "luxury/utils/telegram_helpers.h"
#include "base/base_file_utilities.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "iv/iv_rich_page.h"
#include "main/main_session.h"
#include "storage/file_download_mtproto.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <atomic>
#include <utility>

namespace LuxurySync {

namespace {

bool WaitUntil(
		const std::shared_ptr<TimedCountDownLatch> &latch,
		std::chrono::milliseconds timeout,
		const Cancelled &cancelled) {
	constexpr auto kPoll = std::chrono::milliseconds(250);
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (!cancelled || !cancelled()) {
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now());
		if (remaining <= std::chrono::milliseconds::zero()) {
			return false;
		}
		if (latch->await(std::min(kPoll, remaining))) {
			return true;
		}
	}
	return false;
}

std::shared_ptr<rpl::lifetime> WatchSessionDataClear(
		WeakSession session,
		const std::shared_ptr<std::atomic_bool> &active) {
	const auto lifetime = std::make_shared<rpl::lifetime>();
	crl::on_main_sync([&] {
		if (const auto strong = session.get()) {
			strong->data().sessionDataAboutToBeCleared() | rpl::on_next([=] {
				active->store(false);
			}, *lifetime);
		} else {
			active->store(false);
		}
	});
	return lifetime;
}

template <typename Callback>
bool SendAndWait(
		WeakSession session,
		const Api::SendAction &action,
		int count,
		const Cancelled &cancelled,
		Callback &&callback) {
	Expects(count > 0);
	const auto active = std::make_shared<std::atomic_bool>(true);
	const auto stopped = [=] {
		return !active->load() || !session.get() || (cancelled && cancelled());
	};
	if (stopped()) return false;
	struct State {
		base::flat_set<FullMsgId> pending;
		base::flat_set<FullMsgId> owned;
		int allocated = 0;
		bool dispatching = true;
		bool started = false;
		bool failed = false;
	};
	const auto state = std::make_shared<State>();
	const auto latch = std::make_shared<TimedCountDownLatch>(1);
	const auto lifetime = std::make_shared<rpl::lifetime>();
	crl::on_main(session, [=, callback = std::forward<Callback>(callback)]() mutable {
		if (stopped()) {
			latch->countDown();
			return;
		}
		const auto strong = session.get();
		const auto check = [=] {
			if (!state->dispatching
				&& state->allocated >= count
				&& state->pending.empty()) {
				latch->countDown();
			}
		};
		const auto allocated = std::make_shared<Fn<bool(FullMsgId)>>([=](FullMsgId id) {
			if (stopped()) return false;
			if (state->owned.emplace(id).second) {
				state->pending.emplace(id);
				++state->allocated;
			}
			return true;
		});
		strong->data().sessionDataAboutToBeCleared() | rpl::on_next([=] {
			state->failed = true;
			active->store(false);
			lifetime->destroy();
			latch->countDown();
		}, *lifetime);
		strong->data().itemIdChanged() | rpl::on_next([=](const Data::Session::IdChange &update) {
			if (state->pending.remove(FullMsgId(update.newId.peer, update.oldId))) check();
		}, *lifetime);
		const auto finished = std::make_shared<Fn<void(FullMsgId, bool)>>([=](FullMsgId id, bool success) {
			if (stopped()) return;
			if (!success && (!id || state->pending.contains(id))) {
				state->failed = true;
				active->store(false);
				latch->countDown();
			} else if (state->pending.remove(id)) {
				check();
			}
		});
		strong->uploader().photoFailed() | rpl::on_next([=](FullMsgId id) { (*finished)(id, false); }, *lifetime);
		strong->uploader().documentFailed() | rpl::on_next([=](FullMsgId id) { (*finished)(id, false); }, *lifetime);
		state->started = callback(strong, allocated, finished);
		state->dispatching = false;
		if (state->started) {
			check();
		} else {
			latch->countDown();
		}
	});
	const auto completed = WaitUntil(latch, std::chrono::minutes(5), stopped);
	active->store(false);
	auto result = false;
	crl::on_main_sync([&] {
		lifetime->destroy();
		result = completed && session.get() && !(cancelled && cancelled())
			&& state->started && !state->failed
			&& state->allocated >= count && state->pending.empty();
	});
	return result;
}

QString ForwardPhotoName(not_null<PhotoData*> photo) {
	return "luxury_photo_" + QString::number(photo->getDC()) + "_"
		+ QString::number(photo->id) + ".jpg";
}

constexpr auto kDocumentDownloadTimeout = std::chrono::minutes(15);
[[nodiscard]] bool SameFilePath(const QString &left, const QString &right) {
	const auto leftInfo = QFileInfo(left);
	const auto rightInfo = QFileInfo(right);
	const auto leftCanonical = leftInfo.canonicalFilePath();
	const auto rightCanonical = rightInfo.canonicalFilePath();
	if (!leftCanonical.isEmpty() && !rightCanonical.isEmpty()) {
		return leftCanonical == rightCanonical;
	}
#ifdef Q_OS_WIN
	return leftInfo.absoluteFilePath().compare(
		rightInfo.absoluteFilePath(),
		Qt::CaseInsensitive) == 0;
#else // Q_OS_WIN
	return leftInfo.absoluteFilePath() == rightInfo.absoluteFilePath();
#endif // !Q_OS_WIN
}

[[nodiscard]] bool FileHasExactSize(const QString &path, int64 expectedSize) {
	const auto info = QFileInfo(path);
	return !path.isEmpty()
		&& info.isFile()
		&& info.size() == expectedSize;
}

[[nodiscard]] bool DocumentReady(
		not_null<DocumentData*> document,
		const QString &path) {
	const auto loaded = document->filepath(true);
	return !path.isEmpty()
		&& !loaded.isEmpty()
		&& SameFilePath(path, loaded)
		&& FileHasExactSize(path, document->size);
}

[[nodiscard]] QString GeneratedDocumentName(
		not_null<DocumentData*> document,
		const QString &prefix,
		const QString &extension) {
	return prefix
		+ QString::number(document->getDC())
		+ u"_"_q
		+ QString::number(document->id)
		+ extension;
}

[[nodiscard]] QString DocumentFileName(not_null<DocumentData*> document) {
	if (!document->filename().isEmpty()) {
		return base::FileNameFromUserString(document->filename());
	}
	if (document->isVoiceMessage()) {
		return GeneratedDocumentName(document, u"audio_"_q, u".ogg"_q);
	}
	if (document->isVideoMessage()) {
		return GeneratedDocumentName(document, u"round_"_q, u".mp4"_q);
	}
	if (document->isGifv()) {
		return GeneratedDocumentName(document, u"gif_"_q, u".gif"_q);
	}
	if (document->isVideoFile()) {
		return GeneratedDocumentName(document, u"video_"_q, u".mp4"_q);
	}
	return {};
}

[[nodiscard]] QString NextDocumentPath(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document) {
	const auto directory = pathForSave(session);
	const auto filename = DocumentFileName(document);
	if (directory.isEmpty() || filename.isEmpty()) {
		return {};
	}
	return filedialogNextFilename(filename, QString(), directory);
}

[[nodiscard]] MTPInputMedia UploadedInputMedia(
		const std::shared_ptr<FilePrepareResult> &prepared,
		const Api::RemoteFileInfo &info) {
	if (prepared->type == SendMediaType::Photo) {
		return MTP_inputMediaUploadedPhoto(
			MTP_flags(0),
			info.file,
			MTP_vector<MTPInputDocument>(),
			MTP_int(0),
			MTPInputDocument());
	}

	auto attributes = QVector<MTPDocumentAttribute>();
	prepared->document.match([&](const MTPDdocument &data) {
		attributes = data.vattributes().v;
	}, [](const auto &) {});
	if (attributes.isEmpty()) {
		attributes.push_back(MTP_documentAttributeFilename(
			MTP_string(prepared->filename)));
	}

	using Flag = MTPDinputMediaUploadedDocument::Flag;
	auto flags = MTPDinputMediaUploadedDocument::Flags();
	if (prepared->forceFile) {
		flags |= Flag::f_force_file;
	}
	if (info.thumb) {
		flags |= Flag::f_thumb;
	}
	return MTP_inputMediaUploadedDocument(
		MTP_flags(flags),
		info.file,
		info.thumb.value_or(MTPInputFile()),
		MTP_string(prepared->filemime),
		MTP_vector<MTPDocumentAttribute>(std::move(attributes)),
		MTP_vector<MTPInputDocument>(),
		MTPInputPhoto(),
		MTP_int(0),
		MTP_int(0));
}

} // namespace

QString pathForSave(not_null<Main::Session*> session) {
	auto path = Core::App().settings().downloadPath();
	if (path.isEmpty()) {
		return File::DefaultDownloadPath(session);
	}
	if (path == FileDialog::Tmp()) {
		return session->local().tempDirectory();
	}
	return path;
}

QString documentFileName(not_null<DocumentData*> document) {
	return DocumentFileName(document);
}

QString filePath(not_null<Main::Session*> session, not_null<PhotoData*> photo) {
	const auto directory = pathForSave(session);
	if (directory.isEmpty()) {
		return {};
	}
	return QDir(directory).filePath(ForwardPhotoName(photo));
}

qint64 fileSize(const QString &path) {
	if (path.isEmpty()) {
		return 0;
	}
	QFile file(path);
	return file.exists() ? file.size() : 0;
}

QString filePath(not_null<Main::Session*> session, const Data::Media *media) {
	if (!media) {
		return {};
	}
	const auto directory = QDir(pathForSave(session));

	if (const auto document = media->document()) {
		if (const auto loading = document->loadingFilePath(); !loading.isEmpty()) {
			return loading;
		}
		if (const auto name = document->filepath(true); !name.isEmpty()) {
			return name;
		}
		const auto generatedName = QString("luxury_document_%1_%2")
			.arg(document->getDC())
			.arg(document->id);
		const auto displayName = base::FileNameFromUserString(
			document->filename());
		if (!displayName.isEmpty()) {
			auto name = generatedName;
			auto suffix = QFileInfo(displayName).suffix();
#ifdef Q_OS_WIN
			if (suffix.compare(u"lnk"_q, Qt::CaseInsensitive) == 0
				|| suffix.compare(u"scf"_q, Qt::CaseInsensitive) == 0) {
				suffix += u".download"_q;
			}
#endif // Q_OS_WIN
			if (!suffix.isEmpty()) {
				name += u'.';
				name += suffix;
			}
			return directory.filePath(name);
		}
		if (document->isVoiceMessage()) {
			return directory.filePath(
				"luxury_audio_" + QString::number(document->getDC()) + "_"
				+ QString::number(document->id) + ".ogg");
		}
		if (document->isVideoMessage()) {
			return directory.filePath(
				"luxury_round_" + QString::number(document->getDC()) + "_"
				+ QString::number(document->id) + ".mp4");
		}

		// media without any file name
		if (document->isGifv()) {
			return directory.filePath(
				"luxury_gif_" + QString::number(document->getDC()) + "_"
				+ QString::number(document->id) + ".gif");
		}
		if (document->isVideoFile()) {
			return directory.filePath(
				"luxury_video_" + QString::number(document->getDC()) + "_"
				+ QString::number(document->id) + ".mp4");
		}
		return directory.filePath(generatedName);
	} else if (const auto photo = media->photo()) {
		return directory.filePath(ForwardPhotoName(photo));
	}

	return {};
}

void loadDocumentSync(
		WeakSession session,
		FullMsgId itemId,
		const QString &path,
		const Cancelled &cancelled) {
	if (path.isEmpty()) {
		return;
	}
	auto latch = std::make_shared<TimedCountDownLatch>(1);
	auto lifetime = std::make_shared<rpl::lifetime>();
	const auto active = std::make_shared<std::atomic_bool>(true);
	const auto stopped = [=] {
		return !active->load() || !session.get() || (cancelled && cancelled());
	};
	crl::on_main(session, [=] {
		if (stopped()) {
			latch->countDown();
			return;
		}
		const auto strong = session.get();
		strong->data().sessionDataAboutToBeCleared() | rpl::on_next([=] {
			active->store(false);
			lifetime->destroy();
			latch->countDown();
		}, *lifetime);
		const auto item = strong ? strong->data().message(itemId) : nullptr;
		const auto media = item ? item->media() : nullptr;
		const auto document = media ? media->document() : nullptr;
		if (!document) {
			LOG(("failed to load document for forward"));
			latch->countDown();
			return;
		}
		document->save(Data::FileOriginMessage(itemId), path);

		rpl::single() | rpl::then(
			DocumentLoadUpdates(strong, document)
		) | rpl::filter([=] {
			// Done, failed or cancelled from the download bar: the loader is gone
			// either way, and the caller checks the size on disk afterwards.
			return !document->loading();
		}) | rpl::take(1) | rpl::on_next([latch] {
			latch->countDown();
		}, *lifetime);
	});

	if (!WaitUntil(latch, std::chrono::minutes(15), stopped)) {
		LOG(("forward document download timed out"));
	}

	active->store(false);
	crl::on_main_sync([&] { lifetime->destroy(); });
}

QString loadDocumentSync(
		WeakSession session,
		DocumentId documentId,
		Data::FileOrigin origin,
		const Cancelled &cancelled) {
	const auto active = std::make_shared<std::atomic_bool>(true);
	const auto stopped = [=] {
		return !active->load() || !session.get() || (cancelled && cancelled());
	};
	if (stopped()) return {};
	struct State { QString path; int64 expected = 0; };
	const auto state = std::make_shared<State>();
	auto latch = std::make_shared<TimedCountDownLatch>(1);
	auto lifetime = std::make_shared<rpl::lifetime>();
	crl::on_main(session, [=] {
		if (stopped()) {
			latch->countDown();
			return;
		}
		const auto strong = session.get();
		strong->data().sessionDataAboutToBeCleared() | rpl::on_next([=] {
			active->store(false);
			lifetime->destroy();
			latch->countDown();
		}, *lifetime);
		const auto document = strong->data().document(documentId);
		state->expected = document->size;
		state->path = document->filepath(true);
		if (FileHasExactSize(state->path, state->expected)) { latch->countDown(); return; }
		if (document->loading()) {
			state->path = document->loadingFilePath();
		} else {
			state->path = NextDocumentPath(strong, document);
			if (!state->path.isEmpty() && QDir().mkpath(QFileInfo(state->path).absolutePath())) {
				document->save(origin, state->path);
			} else { state->path.clear(); }
		}
		if (state->path.isEmpty()) { latch->countDown(); return; }
		rpl::single() | rpl::then(DocumentLoadUpdates(strong, document))
		| rpl::filter([=] { return !document->loading(); })
		| rpl::take(1) | rpl::on_next([=] {
			if (document->status == FileDownloadFailed || !DocumentReady(document, state->path)) state->path.clear();
			latch->countDown();
		}, *lifetime);
	});
	const auto finished = WaitUntil(latch, kDocumentDownloadTimeout, stopped);
	const auto successful = finished && !stopped();
	active->store(false);
	crl::on_main_sync([&] { lifetime->destroy(); });
	return successful && FileHasExactSize(state->path, state->expected) ? state->path : QString();
}

bool forwardMessagesSync(WeakSession session,
						 const std::vector<FullMsgId> &itemIds,
						 const ApiWrap::SendAction &action,
						 Data::ForwardOptions options,
						 const Cancelled &cancelled) {
	const auto dataActive = std::make_shared<std::atomic_bool>(true);
	const auto sessionLifetime = WatchSessionDataClear(session, dataActive);
	const auto cleanup = gsl::finally([&] {
		crl::on_main_sync([&] { sessionLifetime->destroy(); });
	});
	const auto active = std::make_shared<std::atomic_bool>(true);
	const auto stopped = [=] {
		return !active->load() || !dataActive->load() || !session.get() || (cancelled && cancelled());
	};
	if (stopped()) {
		return false;
	}
	auto latch = std::make_shared<TimedCountDownLatch>(1);
	auto started = std::make_shared<std::atomic_bool>(false);
	const auto failed = std::make_shared<std::atomic_bool>(false);

	crl::on_main(session, [=] {
		if (stopped()) {
			latch->countDown();
			return;
		}
		const auto strong = session.get();
		if (!strong) {
			return;
		}
		auto items = HistoryItemsList();
		items.reserve(itemIds.size());
		for (const auto &itemId : itemIds) {
			if (const auto item = strong->data().message(itemId)) {
				items.push_back(item);
			}
		}
		if (items.empty()) {
			LOG(("forward has no items to send"));
			latch->countDown();
			return;
		}
		started->store(true);
		auto trackedAction = action;
		trackedAction.options.localSendFinished = std::make_shared<Fn<void(FullMsgId, bool)>>([=](FullMsgId, bool success) {
			if (!success) {
				failed->store(true);
				active->store(false);
				latch->countDown();
			}
		});
		strong->api().forwardMessages(
			Data::ResolvedForwardDraft(items, options),
			trackedAction,
			[latch] { latch->countDown(); });
	});

	const auto completed = WaitUntil(latch, std::chrono::minutes(1), stopped);
	if (!completed && !stopped()) {
		LOG(("forward send timed out"));
	}
	const auto successful = completed && started->load() && !failed->load() && !stopped();
	active->store(false);
	return successful;
}

void loadPhotoSync(
		WeakSession session,
		FullMsgId itemId,
		const QString &path,
		const Cancelled &cancelled) {
	if (path.isEmpty()) {
		return;
	}
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		LOG(("failed to create photo directory for forward"));
		return;
	}

	auto latch = std::make_shared<TimedCountDownLatch>(1);
	auto lifetime = std::make_shared<rpl::lifetime>();
	const auto active = std::make_shared<std::atomic_bool>(true);
	const auto stopped = [=] {
		return !active->load() || !session.get() || (cancelled && cancelled());
	};
	crl::on_main(session, [=] {
		if (stopped()) {
			latch->countDown();
			return;
		}
		const auto strong = session.get();
		strong->data().sessionDataAboutToBeCleared() | rpl::on_next([=] {
			active->store(false);
			lifetime->destroy();
			latch->countDown();
		}, *lifetime);
		const auto item = strong ? strong->data().message(itemId) : nullptr;
		const auto media = item ? item->media() : nullptr;
		const auto photo = media ? media->photo() : nullptr;
		const auto view = photo ? photo->createMediaView() : nullptr;
		if (!view) {
			LOG(("failed to load photo for forward"));
			latch->countDown();
			return;
		}
		photo->clearFailed(Data::PhotoSize::Large);
		view->wanted(Data::PhotoSize::Large, itemId);
		rpl::single() | rpl::then(
			PhotoLoadUpdates(strong, photo)
		) | rpl::filter([=] {
			return view->loaded() || photo->failed(Data::PhotoSize::Large);
		}) | rpl::take(1) | rpl::on_next([=] {
			if (!stopped() && !view->saveToFile(path)) {
				LOG(("failed to save photo for forward to %1").arg(path));
			}
			latch->countDown();
		}, *lifetime);
	});
	if (!WaitUntil(latch, std::chrono::minutes(5), stopped)) {
		LOG(("forward photo download timed out"));
	}
	active->store(false);
	crl::on_main_sync([&] { lifetime->destroy(); });
}

QString loadPhotoSync(
		WeakSession session,
		PhotoId photoId,
		Data::FileOrigin origin,
		const Cancelled &cancelled) {
	const auto active = std::make_shared<std::atomic_bool>(true);
	const auto stopped = [=] {
		return !active->load() || !session.get() || (cancelled && cancelled());
	};
	if (stopped()) return {};
	auto path = std::make_shared<QString>();
	auto latch = std::make_shared<TimedCountDownLatch>(1);
	auto lifetime = std::make_shared<rpl::lifetime>();
	crl::on_main(session, [=] {
		if (stopped()) {
			latch->countDown();
			return;
		}
		const auto strong = session.get();
		strong->data().sessionDataAboutToBeCleared() | rpl::on_next([=] {
			active->store(false);
			lifetime->destroy();
			latch->countDown();
		}, *lifetime);
		const auto photo = strong->data().photo(photoId);
		const auto target = filePath(strong, photo);
		if (target.isEmpty() || !QDir().mkpath(QFileInfo(target).absolutePath())) { latch->countDown(); return; }
		const auto view = photo->createMediaView();
		photo->clearFailed(Data::PhotoSize::Large);
		view->wanted(Data::PhotoSize::Large, origin);
		rpl::single() | rpl::then(PhotoLoadUpdates(strong, photo))
		| rpl::filter([=] {
			return view->loaded() || photo->failed(Data::PhotoSize::Large);
		})
		| rpl::take(1) | rpl::on_next([=] {
			if (!stopped() && view->saveToFile(target)) *path = target;
			latch->countDown();
		}, *lifetime);
	});
	const auto finished = WaitUntil(latch, std::chrono::minutes(5), stopped);
	const auto successful = finished && !stopped();
	active->store(false);
	crl::on_main_sync([&] { lifetime->destroy(); });
	return successful ? *path : QString();
}

bool sendMessageSync(
		WeakSession session,
		Api::MessageToSend &&message,
		const Cancelled &cancelled) {
	const auto action = message.action;
	return SendAndWait(session, action, 1, cancelled, [
		message = std::move(message)
	](
		not_null<Main::Session*> strong,
		const std::shared_ptr<Fn<bool(FullMsgId)>> &allocated,
		const std::shared_ptr<Fn<void(FullMsgId, bool)>> &finished) mutable {
		// we cannot send events to objects
		// owned by a different thread
		// because sendMessage updates UI too

		message.action.options.localIdAllocated = allocated;
		message.action.options.localSendFinished = finished;
		strong->api().sendMessage(std::move(message));
		return true;
	});
}

bool sendDocumentSync(WeakSession session,
					  Ui::PreparedGroup &group,
					  SendMediaType type,
					  TextWithTags &&caption,
					  const Api::SendAction &action,
					  const Cancelled &cancelled) {
	auto groupId = std::make_shared<SendingAlbum>();
	groupId->groupId = base::RandomValue<uint64>();
	const auto count = int(group.list.files.size());

	return SendAndWait(session, action, count, cancelled, [
		groupId,
		type,
		action,
		lst = std::move(group.list),
		caption = std::move(caption)
	](
		not_null<Main::Session*> strong,
		const std::shared_ptr<Fn<bool(FullMsgId)>> &allocated,
		const std::shared_ptr<Fn<void(FullMsgId, bool)>> &finished) mutable {
		auto size = lst.files.size();
		if (!lst.files.empty()) {
			lst.files.front().caption = std::move(caption);
		}
		auto trackedAction = action;
		trackedAction.options.localIdAllocated = allocated;
		trackedAction.options.localSendFinished = finished;
		strong->api().sendFiles(
			std::move(lst),
			type,
			size > 1 ? groupId : nullptr,
			trackedAction);
		return true;
	});
}

bool sendStickerSync(WeakSession session,
					 Api::MessageToSend &&message,
					 FullMsgId itemId,
					 const Cancelled &cancelled) {
	const auto action = message.action;
	return SendAndWait(session, action, 1, cancelled, [
		itemId,
		message = std::move(message)
	](
		not_null<Main::Session*> strong,
		const std::shared_ptr<Fn<bool(FullMsgId)>> &allocated,
		const std::shared_ptr<Fn<void(FullMsgId, bool)>> &finished) mutable {
		const auto item = strong->data().message(itemId);
		const auto media = item ? item->media() : nullptr;
		const auto document = media ? media->document() : nullptr;
		if (!document) {
			return false;
		}
		message.action.options.localIdAllocated = allocated;
		message.action.options.localSendFinished = finished;
		Api::SendExistingDocument(std::move(message), document, std::nullopt);
		return true;
	});
}

bool sendVoiceSync(WeakSession session,
				   const QByteArray &data,
				   int64_t duration,
				   bool video,
				   Api::MessageToSend &&message,
				   const Cancelled &cancelled) {
	const auto action = message.action;

	return SendAndWait(session, action, 1, cancelled, [
		data,
		duration,
		video,
		action,
		message = std::move(message)
	](
		not_null<Main::Session*> strong,
		const std::shared_ptr<Fn<bool(FullMsgId)>> &allocated,
		const std::shared_ptr<Fn<void(FullMsgId, bool)>> &finished) {
		auto options = action.options;
		options.localIdAllocated = allocated;
		options.localSendFinished = finished;
		const auto to = FileLoadTo(
			action.history->peer->id,
			options,
			action.replyTo,
			action.replaceMediaOf);
		strong->api().fileLoader()->addTask(std::make_unique<FileLoadTask>(FileLoadTask::VoiceArgs{
			.session = strong,
			.voice = data,
			.duration = duration,
			.waveform = QVector<signed char>(),
			.video = video,
			.to = to,
			.caption = message.textWithTags
		}));
		return true;
	});
}

UploadedFile uploadFileSync(
		WeakSession session,
		PeerId peerId,
		const QString &path,
		SendMediaType type,
		bool forceFile,
		const QString &displayName,
		const Cancelled &cancelled) {
	const auto dataActive = std::make_shared<std::atomic_bool>(true);
	const auto sessionLifetime = WatchSessionDataClear(session, dataActive);
	const auto sessionCleanup = gsl::finally([&] {
		crl::on_main_sync([&] { sessionLifetime->destroy(); });
	});
	const auto active = std::make_shared<std::atomic_bool>(true);
	const auto stopped = [=] {
		return !active->load() || !dataActive->load() || !session.get() || (cancelled && cancelled());
	};
	if (stopped() || path.isEmpty() || !QFile::exists(path)) return {};
	std::unique_ptr<FileLoadTask> task;
	crl::on_main_sync([&] {
		if (stopped()) return;
		const auto strong = session.get();
		task = std::make_unique<FileLoadTask>(FileLoadTask::Args{
			.session = strong,
			.filepath = path,
			.type = type,
			.to = FileLoadTo(peerId, Api::SendOptions(), FullReplyTo(), MsgId()),
			.forceFile = forceFile,
			.sendLargePhotos = (type == SendMediaType::Photo),
			.displayName = displayName,
		});
	});
	if (!task || stopped()) return {};
	task->process({ .generateGoodThumbnail = false });
	const auto prepared = task->peekResult();
	if (!prepared || stopped()) return {};
	const auto info = std::make_shared<std::optional<Api::RemoteFileInfo>>();
	const auto uploadId = std::make_shared<FullMsgId>();
	auto latch = std::make_shared<TimedCountDownLatch>(1);
	auto lifetime = std::make_shared<rpl::lifetime>();
	crl::on_main(session, [=] {
		if (stopped()) {
			latch->countDown();
			return;
		}
		const auto strong = session.get();
		*uploadId = FullMsgId(peerId, strong->data().nextLocalMessageId());
		const auto ready = [=](const Storage::UploadedMedia &data) {
			if (!stopped() && data.fullId == *uploadId) {
				*info = data.info;
				latch->countDown();
			}
		};
		const auto failed = [=](const FullMsgId &id) {
			if (id == *uploadId) latch->countDown();
		};
		strong->data().sessionDataAboutToBeCleared() | rpl::on_next([=] {
			lifetime->destroy();
			latch->countDown();
		}, *lifetime);
		strong->uploader().photoReady() | rpl::on_next(ready, *lifetime);
		strong->uploader().documentReady() | rpl::on_next(ready, *lifetime);
		strong->uploader().photoFailed() | rpl::on_next(failed, *lifetime);
		strong->uploader().documentFailed() | rpl::on_next(failed, *lifetime);
		strong->uploader().upload(*uploadId, prepared);
	});
	const auto uploaded = WaitUntil(latch, std::chrono::minutes(30), stopped);
	auto uploadInfo = std::optional<Api::RemoteFileInfo>();
	active->store(false);
	crl::on_main_sync([&] {
		lifetime->destroy();
		if (const auto strong = session.get()) {
			if (!uploaded && *uploadId) strong->uploader().cancel(*uploadId);
			if (uploaded) uploadInfo = *info;
		}
	});
	if (!uploadInfo || !dataActive->load() || !session.get() || (cancelled && cancelled())) return {};
	const auto mediaActive = std::make_shared<std::atomic_bool>(true);
	const auto mediaStopped = [=] {
		return !mediaActive->load() || !dataActive->load() || !session.get() || (cancelled && cancelled());
	};
	auto result = std::make_shared<UploadedFile>();
	auto request = std::make_shared<mtpRequestId>(0);
	auto mediaLatch = std::make_shared<TimedCountDownLatch>(1);
	crl::on_main(session, [=] {
		if (mediaStopped()) {
			mediaLatch->countDown();
			return;
		}
		const auto strong = session.get();
		const auto peer = strong->data().peer(peerId);
		*request = strong->api().request(MTPmessages_UploadMedia(
			MTP_flags(0), MTPstring(), peer->input(),
			UploadedInputMedia(prepared, *uploadInfo)
		)).done([=](const MTPMessageMedia &media) {
			if (mediaStopped()) {
				mediaLatch->countDown();
				return;
			}
			const auto owner = session.get();
			media.match([&](const MTPDmessageMediaPhoto &data) {
				const auto photo = data.vphoto();
				if (photo && photo->type() == mtpc_photo) result->photo = owner->data().processPhoto(*photo)->id;
			}, [&](const MTPDmessageMediaDocument &data) {
				const auto document = data.vdocument();
				if (document && document->type() == mtpc_document) result->document = owner->data().processDocument(*document)->id;
			}, [](const auto &) {});
			*request = 0;
			mediaLatch->countDown();
		}).fail([=](const MTP::Error &) {
			*request = 0;
			mediaLatch->countDown();
		}).send();
	});
	const auto finished = WaitUntil(mediaLatch, std::chrono::minutes(5), mediaStopped);
	mediaActive->store(false);
	auto snapshot = UploadedFile();
	crl::on_main_sync([&] {
		if (const auto strong = session.get()) {
			if (*request) strong->api().request(*request).cancel();
			if (finished && dataActive->load() && !(cancelled && cancelled())) snapshot = *result;
		}
	});
	return snapshot;
}

std::shared_ptr<const Iv::RichPage> loadFullRichPageSync(
		WeakSession session,
		FullMsgId itemId,
		const Cancelled &cancelled) {
	const auto dataActive = std::make_shared<std::atomic_bool>(true);
	const auto sessionLifetime = WatchSessionDataClear(session, dataActive);
	const auto sessionCleanup = gsl::finally([&] {
		crl::on_main_sync([&] { sessionLifetime->destroy(); });
	});
	const auto active = std::make_shared<std::atomic_bool>(true);
	const auto stopped = [=] {
		return !active->load() || !dataActive->load() || !session.get() || (cancelled && cancelled());
	};
	if (stopped()) return nullptr;
	const auto request = std::make_shared<mtpRequestId>(0);
	const auto resolved = std::make_shared<
		std::shared_ptr<const Iv::RichPage>>();
	auto latch = std::make_shared<TimedCountDownLatch>(1);

	crl::on_main(session, [=] {
		if (stopped()) {
			latch->countDown();
			return;
		}
		const auto strong = session.get();
		const auto item = strong->data().message(itemId);
		if (!item) {
			latch->countDown();
			return;
		}
		if (const auto full = item->fullRichPage()) {
			*resolved = full;
			latch->countDown();
			return;
		}

		const auto page = item->richPage();
		if (!page) {
			latch->countDown();
			return;
		}
		if (!page->part || item->isLocal() || item->isDeleted()) {
			*resolved = page;
			latch->countDown();
			return;
		}

		const auto peer = item->history()->peer;

		*request = strong->api().request(MTPmessages_GetRichMessage(
			peer->input(),
			MTP_int(itemId.msg)
		)).done([=](const MTPmessages_Messages &result) {
			if (stopped()) {
				latch->countDown();
				return;
			}
			const auto owner = session.get();
			*request = 0;
			auto full = std::shared_ptr<const Iv::RichPage>();
			const auto process = [&](const auto &data) {
				owner->data().processUsers(data.vusers());
				owner->data().processChats(data.vchats());
				peer->processTopics(data.vtopics());
				for (const auto &message : data.vmessages().v) {
					if (message.type() != mtpc_message) {
						continue;
					}
					const auto &fields = message.c_message();
					if (MsgId(fields.vid().v) != itemId.msg) {
						continue;
					}
					if (const auto richMessage = fields.vrich_message()) {
						full = Iv::ParseRichPage(owner, *richMessage);
					}
					break;
				}
			};
			result.match([](const MTPDmessages_messagesNotModified &) {
			}, [&](const MTPDmessages_channelMessages &data) {
				process(data);
				if (const auto channel = peer->asChannel()) {
					channel->ptsReceived(data.vpts().v);
				}
			}, [&](const auto &data) {
				process(data);
			});
			if (full) {
				if (const auto current = owner->data().message(itemId)) {
					current->setFullRichPage(full);
				}
				*resolved = full;
			}
			latch->countDown();
		}).fail([=](const MTP::Error &) {
			*request = 0;
			latch->countDown();
		}).send();
	});

	const auto finished = WaitUntil(latch, std::chrono::minutes(1), stopped);
	active->store(false);
	auto snapshot = std::shared_ptr<const Iv::RichPage>();
	crl::on_main_sync([&] {
		if (const auto strong = session.get()) {
			if (*request) strong->api().request(*request).cancel();
			if (finished && dataActive->load() && !(cancelled && cancelled())) snapshot = *resolved;
		}
	});
	return snapshot;
}

bool sendRichMessageSync(
		WeakSession session,
		const MTPInputRichMessage &richMessage,
		const Api::SendAction &action,
		const Cancelled &cancelled) {
	const auto dataActive = std::make_shared<std::atomic_bool>(true);
	const auto sessionLifetime = WatchSessionDataClear(session, dataActive);
	const auto sessionCleanup = gsl::finally([&] {
		crl::on_main_sync([&] { sessionLifetime->destroy(); });
	});
	const auto active = std::make_shared<std::atomic_bool>(true);
	const auto stopped = [=] {
		return !active->load() || !dataActive->load() || !session.get() || (cancelled && cancelled());
	};
	if (stopped()) return false;
	const auto request = std::make_shared<mtpRequestId>(0);
	const auto sent = std::make_shared<bool>(false);
	auto latch = std::make_shared<TimedCountDownLatch>(1);

	crl::on_main(session, [=] {
		if (stopped()) {
			latch->countDown();
			return;
		}
		const auto strong = session.get();
		// The same as a text message of this forward gets from sendMessage():
		// no effect or suggestion of its own, a reply that the server still
		// knows, ghost mode's schedule date and the price of a paid chat.
		auto sending = action;
		sending.options.suggest = {};
		sending.options.effectId = 0;
		StripUnsendableReply(strong, sending.replyTo);
		applyGhostScheduling(strong, sending.options);
		const auto peer = sending.history->peer;
		const auto starsPaid = std::min(
			peer->starsPerMessageChecked(),
			sending.options.starsApproved);

		using Flag = MTPmessages_SendMessage::Flag;
		auto sendFlags = MTPmessages_SendMessage::Flags(0)
			| Flag::f_rich_message;
		if (sending.replyTo) {
			sendFlags |= Flag::f_reply_to;
		}
		if (ShouldSendSilent(peer, sending.options)) {
			sendFlags |= Flag::f_silent;
		}
		if (sending.options.scheduled) {
			sendFlags |= Flag::f_schedule_date;
			if (sending.options.scheduleRepeatPeriod) {
				sendFlags |= Flag::f_schedule_repeat_period;
			}
		}
		if (sending.options.sendAs) {
			sendFlags |= Flag::f_send_as;
		}
		if (starsPaid) {
			sendFlags |= Flag::f_allow_paid_stars;
		}

		*request = strong->api().request(MTPmessages_SendMessage(
			MTP_flags(sendFlags),
			peer->input(),
			sending.mtpReplyTo(),
			MTP_string(QString()),
			MTP_long(base::RandomValue<uint64>()),
			MTPReplyMarkup(),
			MTPVector<MTPMessageEntity>(),
			MTP_int(sending.options.scheduled),
			MTP_int(sending.options.scheduleRepeatPeriod),
			(sending.options.sendAs
				? sending.options.sendAs->input()
				: MTP_inputPeerEmpty()),
			MTPInputQuickReplyShortcut(),
			MTP_long(0), // effect
			MTP_long(starsPaid),
			Api::SuggestToMTP(sending.options.suggest),
			richMessage
		)).done([=](const MTPUpdates &result) {
			*request = 0;
			if (stopped()) {
				latch->countDown();
				return;
			}
			session.get()->api().applyUpdates(result);
			*sent = true;
			latch->countDown();
		}).fail([=](const MTP::Error &error) {
			*request = 0;
			LOG(("LuxuryForward: rich message send failed: %1").arg(error.type()));
			latch->countDown();
		}).send();
	});

	const auto finished = WaitUntil(latch, std::chrono::minutes(2), stopped);
	active->store(false);
	auto snapshot = false;
	crl::on_main_sync([&] {
		if (const auto strong = session.get()) {
			if (*request) strong->api().request(*request).cancel();
			if (finished && dataActive->load() && !(cancelled && cancelled())) snapshot = *sent;
		}
	});
	return snapshot;
}

} // namespace LuxurySync
