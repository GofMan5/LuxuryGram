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
#include "storage/localimageloader.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <atomic>
#include <mutex>
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

template <typename Callback>
bool SendAndWait(
		WeakSession session,
		const Api::SendAction &action,
		int count,
		const Cancelled &cancelled,
		Callback &&callback) {
	Expects(count > 0);
	const auto stopped = [=] {
		return !session.get() || (cancelled && cancelled());
	};
	if (stopped()) {
		return false;
	}

	auto latch = std::make_shared<TimedCountDownLatch>(count);
	auto lifetime = std::make_shared<rpl::lifetime>();
	auto started = std::make_shared<std::atomic_bool>(false);

	crl::on_main(session, [
		session,
		action,
		count,
		latch,
		lifetime,
		started,
		stopped,
		callback = std::forward<Callback>(callback)
	]() mutable {
		if (stopped()) {
			return;
		}
		const auto strong = session.get();
		if (!strong) {
			return;
		}
		const auto peerId = action.history->peer->id;
		strong->data().itemIdChanged(
		) | rpl::filter([peerId](const Data::Session::IdChange &update) {
			return peerId == update.newId.peer;
		}) | rpl::take(count) | rpl::on_next([latch] {
			latch->countDown();
		}, *lifetime);
		started->store(true);
		if (!callback(strong)) {
			started->store(false);
			for (auto i = 0; i != count; ++i) {
				latch->countDown();
			}
		}
	});

	const auto completed = WaitUntil(
		latch,
		std::chrono::minutes(5),
		stopped);
	crl::on_main([lifetime = std::move(lifetime)] {
		lifetime->destroy();
	});
	return completed && started->load();
}

QString ForwardPhotoName(not_null<PhotoData*> photo) {
	return "luxury_photo_" + QString::number(photo->getDC()) + "_"
		+ QString::number(photo->id) + ".jpg";
}

constexpr auto kDocumentDownloadTimeout = std::chrono::minutes(15);
constexpr auto kDownloadWaitStep = std::chrono::milliseconds(100);

enum class DownloadWaitResult {
	Completed,
	Cancelled,
	TimedOut,
};

struct ActiveDocumentDownload {
	QString path;
	QString readyPath;
	TimedCountDownLatch done{ 1 };
	rpl::lifetime lifetime;
	int consumers = 1;
	bool allowSizeFallback = false;
};

struct DocumentDownloadRegistration {
	std::shared_ptr<ActiveDocumentDownload> state;
	bool owner = false;
};

using ActiveDocumentDownloads = base::flat_map<
	DocumentData*,
	std::shared_ptr<ActiveDocumentDownload>>;

[[nodiscard]] std::mutex &DocumentDownloadsMutex() {
	static auto result = std::mutex();
	return result;
}

[[nodiscard]] ActiveDocumentDownloads &DocumentDownloads() {
	static auto result = ActiveDocumentDownloads();
	return result;
}

[[nodiscard]] DocumentDownloadRegistration RegisterDocumentDownload(
		not_null<DocumentData*> document) {
	const auto lock = std::lock_guard(DocumentDownloadsMutex());
	auto &active = DocumentDownloads();
	const auto i = active.find(document);
	if (i != active.end()) {
		++i->second->consumers;
		return { i->second, false };
	}
	auto state = std::make_shared<ActiveDocumentDownload>();
	active.emplace(document, state);
	return { std::move(state), true };
}

void FinishDocumentDownload(
		DocumentData *document,
		const std::shared_ptr<ActiveDocumentDownload> &state) {
	{
		const auto lock = std::lock_guard(DocumentDownloadsMutex());
		auto &active = DocumentDownloads();
		const auto i = active.find(document);
		if (i != active.end() && i->second == state) {
			active.erase(i);
		}
	}
	state->done.countDown();
}

void ReleaseDocumentDownloadConsumer(
		DocumentData *document,
		const std::shared_ptr<ActiveDocumentDownload> &state) {
	auto released = false;
	{
		const auto lock = std::lock_guard(DocumentDownloadsMutex());
		Expects(state->consumers > 0);
		--state->consumers;
		if (state->consumers == 0) {
			auto &active = DocumentDownloads();
			const auto i = active.find(document);
			if (i != active.end() && i->second == state) {
				active.erase(i);
				released = true;
			}
		}
	}
	if (released) {
		state->done.countDown();
	}
}

[[nodiscard]] DownloadWaitResult WaitForDownload(
		TimedCountDownLatch &done,
		std::chrono::milliseconds timeout,
		const Fn<bool()> &cancelled) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (true) {
		if (done.await(std::chrono::milliseconds(0))) {
			return DownloadWaitResult::Completed;
		}
		if (cancelled && cancelled()) {
			return DownloadWaitResult::Cancelled;
		}
		const auto remaining = std::chrono::duration_cast<
			std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now());
		if (remaining <= std::chrono::milliseconds(0)) {
			return DownloadWaitResult::TimedOut;
		}
		if (done.await(std::min(
				remaining,
				kDownloadWaitStep))) {
			return DownloadWaitResult::Completed;
		}
	}
}

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

[[nodiscard]] QString LoadedDocumentPath(not_null<DocumentData*> document) {
	auto result = QString();
	crl::on_main_sync([&] {
		result = document->filepath(true);
	});
	return result;
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

void CompleteDocumentDownload(
		not_null<DocumentData*> document,
		const std::shared_ptr<ActiveDocumentDownload> &state) {
	if (document->loading()) {
		return;
	}
	const auto released = state->done.await(std::chrono::milliseconds(0));
	const auto ready = state->allowSizeFallback
		? FileHasExactSize(state->path, document->size)
		: (document->status != FileDownloadFailed
			&& !document->cancelled()
			&& DocumentReady(document, state->path));
	if (!ready) {
		QFile::remove(state->path);
		state->path.clear();
	} else if (!released) {
		state->readyPath = state->path;
	}
	state->lifetime.destroy();
	if (!released) {
		FinishDocumentDownload(document, state);
	}
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
		qint64 expectedSize,
		const Cancelled &cancelled) {
	if (path.isEmpty()) {
		return;
	}
	auto latch = std::make_shared<TimedCountDownLatch>(1);
	auto lifetime = std::make_shared<rpl::lifetime>();
	const auto stopped = [=] {
		return !session.get() || (cancelled && cancelled());
	};
	crl::on_main(session, [=] {
		if (stopped()) {
			latch->countDown();
			return;
		}
		const auto strong = session.get();
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
			strong->downloaderTaskFinished()
		) | rpl::filter([=] {
			// The stat is the only way to know the file is fully written, but it
			// must not run on every downloader tick in the app: while this one is
			// still loading, the answer cannot be yes.
			return document->status == FileDownloadFailed
				|| (!document->loading()
					&& QFileInfo(path).size() == expectedSize);
		}) | rpl::take(1) | rpl::on_next([latch] {
			latch->countDown();
		}, *lifetime);
	});

	if (!WaitUntil(latch, std::chrono::minutes(15), stopped)) {
		LOG(("forward document download timed out"));
	}

	crl::on_main([lifetime = std::move(lifetime)] {
		lifetime->destroy();
	});
}

QString loadDocumentSync(
		not_null<Main::Session*> session,
		not_null<DocumentData*> data,
		Data::FileOrigin origin,
		const Cancelled &cancelled) {
	if (cancelled && cancelled()) {
		return {};
	}
	const auto expectedSize = data->size;
	const auto registration = RegisterDocumentDownload(data);
	const auto releaseConsumer = gsl::finally([&] {
		ReleaseDocumentDownloadConsumer(data, registration.state);
	});
	if (!registration.owner) {
		const auto waitResult = WaitForDownload(
			registration.state->done,
			kDocumentDownloadTimeout,
			cancelled);
		if (waitResult != DownloadWaitResult::Completed) {
			if (waitResult != DownloadWaitResult::Cancelled) {
				LOG(("forward document download timed out"));
			}
			return {};
		}
		const auto path = registration.state->readyPath;
		return FileHasExactSize(path, expectedSize) ? path : QString();
	}
	auto registrationActive = true;
	auto reserved = false;
	const auto cleanup = gsl::finally([&] {
		if (!registrationActive) {
			return;
		}
		if (reserved) {
			QFile::remove(registration.state->path);
		}
		registration.state->path.clear();
		FinishDocumentDownload(data, registration.state);
	});
	auto path = LoadedDocumentPath(data);
	if (FileHasExactSize(path, expectedSize)) {
		registration.state->path = path;
		registration.state->readyPath = path;
		registrationActive = false;
		FinishDocumentDownload(data, registration.state);
		return path;
	}
	while (true) {
		path = NextDocumentPath(session, data);
		if (path.isEmpty()) {
			LOG(("failed to pick document path for forward"));
			return {};
		}
		if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
			LOG(("failed to create document directory for forward"));
			return {};
		}
		auto reservation = QFile(path);
		if (reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
			reservation.close();
			reserved = true;
			break;
		}
		if (!QFile::exists(path)) {
			LOG(("failed to reserve document file for forward"));
			return {};
		}
	}
	registration.state->path = path;
	if (cancelled && cancelled()) {
		return {};
	}

	const auto state = registration.state;
	const auto documentId = data->id;
	crl::on_main_sync([=] {
		data->save(origin, path);
		state->allowSizeFallback = !data->loading();
		if (!data->loading()) {
			CompleteDocumentDownload(data, state);
			return;
		}

		session->data().documentLoadProgress(
		) | rpl::filter([=](not_null<DocumentData*> changed) {
			return changed->id == documentId
				&& !changed->loading();
		}) | rpl::on_next([=](not_null<DocumentData*> changed) {
			CompleteDocumentDownload(changed, state);
		}, state->lifetime);
	});
	registrationActive = false;
	const auto waitResult = WaitForDownload(
		registration.state->done,
		kDocumentDownloadTimeout,
		cancelled);
	if (waitResult != DownloadWaitResult::Completed) {
		if (waitResult != DownloadWaitResult::Cancelled) {
			LOG(("forward document download timed out"));
		}
		return {};
	}
	path = registration.state->readyPath;
	return FileHasExactSize(path, expectedSize) ? path : QString();
}

bool forwardMessagesSync(WeakSession session,
						 const std::vector<FullMsgId> &itemIds,
						 const ApiWrap::SendAction &action,
						 Data::ForwardOptions options,
						 const Cancelled &cancelled) {
	const auto stopped = [=] {
		return !session.get() || (cancelled && cancelled());
	};
	if (stopped()) {
		return false;
	}
	auto latch = std::make_shared<TimedCountDownLatch>(1);
	auto started = std::make_shared<std::atomic_bool>(false);

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
		strong->api().forwardMessages(
			Data::ResolvedForwardDraft(items, options),
			action,
			[latch] { latch->countDown(); });
	});

	const auto completed = WaitUntil(latch, std::chrono::minutes(1), stopped);
	if (!completed && !stopped()) {
		LOG(("forward send timed out"));
	}
	return completed && started->load();
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
	const auto stopped = [=] {
		return !session.get() || (cancelled && cancelled());
	};
	crl::on_main(session, [=] {
		if (stopped()) {
			latch->countDown();
			return;
		}
		const auto strong = session.get();
		const auto item = strong ? strong->data().message(itemId) : nullptr;
		const auto media = item ? item->media() : nullptr;
		const auto photo = media ? media->photo() : nullptr;
		const auto view = photo ? photo->createMediaView() : nullptr;
		if (!view) {
			LOG(("failed to load photo for forward"));
			latch->countDown();
			return;
		}
		view->wanted(Data::PhotoSize::Large, itemId);
		rpl::single() | rpl::then(
			strong->downloaderTaskFinished()
		) | rpl::filter([view] {
			return view->loaded();
		}) | rpl::take(1) | rpl::on_next([=] {
			view->saveToFile(path);
			latch->countDown();
		}, *lifetime);
	});
	if (!WaitUntil(latch, std::chrono::minutes(5), stopped)) {
		LOG(("forward photo download timed out"));
	}
	crl::on_main([lifetime = std::move(lifetime)] {
		lifetime->destroy();
	});
}

void loadPhotoSync(
		not_null<Main::Session*> session,
		not_null<PhotoData*> photo,
		Data::FileOrigin origin,
		const Cancelled &cancelled) {
	const auto path = pathForSave(session);
	if (path.isEmpty() || !QDir().mkpath(path)) {
		LOG(("failed to prepare photo path for forward"));
		return;
	}

	auto latch = std::make_shared<TimedCountDownLatch>(1);
	auto lifetime = std::make_shared<rpl::lifetime>();

	crl::on_main(session, [=] {
		const auto view = photo->createMediaView();
		if (!view) {
			latch->countDown();
			return;
		}
		view->wanted(Data::PhotoSize::Large, origin);

		const auto saveToFiles = [=] {
			view->saveToFile(filePath(session, photo));
		};

		if (view->loaded()) {
			saveToFiles();
			latch->countDown();
			return;
		}

		rpl::single()
		| rpl::then(session->downloaderTaskFinished())
		| rpl::filter([view] { return view->loaded(); })
		| rpl::take(1)
		| rpl::on_next([=] {
			saveToFiles();
			latch->countDown();
		}, *lifetime);
	});

	if (!WaitUntil(latch, std::chrono::minutes(5), cancelled)) {
		LOG(("forward photo download timed out"));
	}

	crl::on_main([lifetime = std::move(lifetime)] {
		lifetime->destroy();
	});
}

bool sendMessageSync(
		WeakSession session,
		Api::MessageToSend &&message,
		const Cancelled &cancelled) {
	const auto action = message.action;
	return SendAndWait(session, action, 1, cancelled, [
		message = std::move(message)
	](not_null<Main::Session*> strong) mutable {
		// we cannot send events to objects
		// owned by a different thread
		// because sendMessage updates UI too

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
	](not_null<Main::Session*> strong) mutable {
		auto size = lst.files.size();
		if (!lst.files.empty()) {
			lst.files.front().caption = std::move(caption);
		}
		strong->api().sendFiles(
			std::move(lst),
			type,
			size > 1 ? groupId : nullptr,
			action);
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
	](not_null<Main::Session*> strong) mutable {
		const auto item = strong->data().message(itemId);
		const auto media = item ? item->media() : nullptr;
		const auto document = media ? media->document() : nullptr;
		if (!document) {
			return false;
		}
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
	](not_null<Main::Session*> strong) {
		const auto to = FileLoadTo(
			action.history->peer->id,
			action.options,
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
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		const QString &path,
		SendMediaType type,
		bool forceFile,
		const QString &displayName) {
	if (path.isEmpty() || !QFile::exists(path)) {
		return {};
	}

	auto task = FileLoadTask(FileLoadTask::Args{
		.session = session,
		.filepath = path,
		.type = type,
		.to = FileLoadTo(peer->id, Api::SendOptions(), FullReplyTo(), MsgId()),
		.forceFile = forceFile,
		.sendLargePhotos = (type == SendMediaType::Photo),
		.displayName = displayName,
	});
	task.process({ .generateGoodThumbnail = false });

	const auto prepared = task.peekResult();
	if (!prepared) {
		return {};
	}

	const auto info = std::make_shared<std::optional<Api::RemoteFileInfo>>();
	auto uploadLatch = std::make_shared<TimedCountDownLatch>(1);
	auto lifetime = std::make_shared<rpl::lifetime>();

	crl::on_main(session, [=] {
		const auto uploadId = FullMsgId(
			peer->id,
			session->data().nextLocalMessageId());
		const auto ready = [=](const Storage::UploadedMedia &data) {
			if (data.fullId != uploadId) {
				return;
			}
			*info = data.info;
			uploadLatch->countDown();
		};
		const auto failed = [=](const FullMsgId &id) {
			if (id == uploadId) {
				uploadLatch->countDown();
			}
		};

		session->uploader().photoReady() | rpl::on_next(ready, *lifetime);
		session->uploader().documentReady() | rpl::on_next(ready, *lifetime);
		session->uploader().photoFailed() | rpl::on_next(failed, *lifetime);
		session->uploader().documentFailed() | rpl::on_next(failed, *lifetime);

		session->uploader().upload(uploadId, prepared);
	});

	const auto uploadFinished = uploadLatch->await(std::chrono::minutes(30));
	crl::on_main([lifetime = std::move(lifetime)] {
		lifetime->destroy();
	});

	if (!uploadFinished || !info->has_value()) {
		return {};
	}

	const auto result = std::make_shared<UploadedFile>();
	auto mediaLatch = std::make_shared<TimedCountDownLatch>(1);

	crl::on_main(session, [=] {
		session->api().request(MTPmessages_UploadMedia(
			MTP_flags(0),
			MTPstring(),
			peer->input(),
			UploadedInputMedia(prepared, **info)
		)).done([=](const MTPMessageMedia &media) {
			media.match([&](const MTPDmessageMediaPhoto &data) {
				const auto photo = data.vphoto();
				if (photo && photo->type() == mtpc_photo) {
					result->photo = session->data().processPhoto(*photo);
				}
			}, [&](const MTPDmessageMediaDocument &data) {
				const auto document = data.vdocument();
				if (document && document->type() == mtpc_document) {
					result->document = session->data().processDocument(*document);
				}
			}, [](const auto &) {});
			mediaLatch->countDown();
		}).fail([=](const MTP::Error &) {
			mediaLatch->countDown();
		}).send();
	});

	const auto mediaFinished = mediaLatch->await(std::chrono::minutes(5));

	return mediaFinished ? *result : UploadedFile();
}

std::shared_ptr<const Iv::RichPage> loadFullRichPageSync(
		not_null<Main::Session*> session,
		FullMsgId itemId) {
	const auto resolved = std::make_shared<
		std::shared_ptr<const Iv::RichPage>>();
	auto latch = std::make_shared<TimedCountDownLatch>(1);

	crl::on_main(session, [=] {
		const auto item = session->data().message(itemId);
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

		session->api().request(MTPmessages_GetRichMessage(
			peer->input(),
			MTP_int(itemId.msg)
		)).done([=](const MTPmessages_Messages &result) {
			auto full = std::shared_ptr<const Iv::RichPage>();
			const auto process = [&](const auto &data) {
				session->data().processUsers(data.vusers());
				session->data().processChats(data.vchats());
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
						full = Iv::ParseRichPage(session, *richMessage);
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
				if (const auto current = session->data().message(itemId)) {
					current->setFullRichPage(full);
				}
				*resolved = full;
			}
			latch->countDown();
		}).fail([=](const MTP::Error &) {
			latch->countDown();
		}).send();
	});

	const auto finished = latch->await(std::chrono::minutes(1));

	return finished ? *resolved : nullptr;
}

bool sendRichMessageSync(
		not_null<Main::Session*> session,
		const MTPInputRichMessage &richMessage,
		const Api::SendAction &action) {
	const auto sent = std::make_shared<bool>(false);
	auto latch = std::make_shared<TimedCountDownLatch>(1);

	crl::on_main(session, [=] {
		const auto peer = action.history->peer;

		using Flag = MTPmessages_SendMessage::Flag;
		auto sendFlags = MTPmessages_SendMessage::Flags(0)
			| Flag::f_rich_message;
		if (action.replyTo) {
			sendFlags |= Flag::f_reply_to;
		}
		if (ShouldSendSilent(peer, action.options)) {
			sendFlags |= Flag::f_silent;
		}
		if (action.options.scheduled) {
			sendFlags |= Flag::f_schedule_date;
			if (action.options.scheduleRepeatPeriod) {
				sendFlags |= Flag::f_schedule_repeat_period;
			}
		}
		if (action.options.sendAs) {
			sendFlags |= Flag::f_send_as;
		}
		if (action.options.effectId) {
			sendFlags |= Flag::f_effect;
		}

		session->api().request(MTPmessages_SendMessage(
			MTP_flags(sendFlags),
			peer->input(),
			action.mtpReplyTo(),
			MTP_string(QString()),
			MTP_long(base::RandomValue<uint64>()),
			MTPReplyMarkup(),
			MTPVector<MTPMessageEntity>(),
			MTP_int(action.options.scheduled),
			MTP_int(action.options.scheduleRepeatPeriod),
			(action.options.sendAs
				? action.options.sendAs->input()
				: MTP_inputPeerEmpty()),
			MTPInputQuickReplyShortcut(),
			MTP_long(action.options.effectId),
			MTP_long(0),
			Api::SuggestToMTP(action.options.suggest),
			richMessage
		)).done([=](const MTPUpdates &result) {
			session->api().applyUpdates(result);
			*sent = true;
			latch->countDown();
		}).fail([=](const MTP::Error &error) {
			LOG(("LuxuryForward: rich message send failed: %1").arg(error.type()));
			latch->countDown();
		}).send();
	});

	const auto finished = latch->await(std::chrono::minutes(2));

	return finished && *sent;
}

} // namespace LuxurySync
