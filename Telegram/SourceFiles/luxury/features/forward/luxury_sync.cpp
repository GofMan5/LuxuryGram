// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/features/forward/luxury_sync.h"

#include "apiwrap.h"
#include "api/api_sending.h"
#include "luxury/utils/telegram_helpers.h"
#include "base/base_file_utilities.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/file_download_mtproto.h"
#include "storage/localimageloader.h"

#include <QFileInfo>

#include <atomic>

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
			latch->countDown();
			return;
		}
		document->save(Data::FileOriginMessage(itemId), path);

		rpl::single() | rpl::then(
			strong->downloaderTaskFinished()
		) | rpl::filter([=] {
			return document->status == FileDownloadFailed
				|| QFileInfo(path).size() == expectedSize;
		}) | rpl::take(1) | rpl::on_next([latch] {
			latch->countDown();
		}, *lifetime);
	});

	WaitUntil(latch, std::chrono::minutes(15), stopped);

	crl::on_main([lifetime = std::move(lifetime)] {
		lifetime->destroy();
	});
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
			latch->countDown();
			return;
		}
		started->store(true);
		strong->api().forwardMessages(
			Data::ResolvedForwardDraft(items, options),
			action,
			[latch] { latch->countDown(); });
	});

	return WaitUntil(latch, std::chrono::minutes(1), stopped)
		&& started->load();
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
	WaitUntil(latch, std::chrono::minutes(5), stopped);
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

} // namespace LuxurySync
