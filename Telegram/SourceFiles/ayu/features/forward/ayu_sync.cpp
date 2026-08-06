// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/forward/ayu_sync.h"

#include "apiwrap.h"
#include "api/api_sending.h"
#include "ayu/utils/telegram_helpers.h"
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

namespace LuxurySync {

namespace {

template <typename Callback>
void SendAndWait(
		not_null<Main::Session*> session,
		const Api::SendAction &action,
		int count,
		Callback &&callback) {
	Expects(count > 0);

	auto latch = std::make_shared<TimedCountDownLatch>(count);
	auto lifetime = std::make_shared<rpl::lifetime>();

	crl::on_main(session, [
		session,
		action,
		count,
		latch,
		lifetime,
		callback = std::forward<Callback>(callback)
	]() mutable {
		const auto peerId = action.history->peer->id;
		session->data().itemIdChanged(
		) | rpl::filter([peerId](const Data::Session::IdChange &update) {
			return peerId == update.newId.peer;
		}) | rpl::take(count) | rpl::on_next([latch] {
			latch->countDown();
		}, *lifetime);
		callback();
	});

	latch->await(std::chrono::minutes(5));
	crl::on_main([lifetime = std::move(lifetime)] {
		lifetime->destroy();
	});
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

qint64 fileSize(not_null<HistoryItem*> item) {
	if (const auto path = filePath(&item->history()->session(), item->media()); !path.isEmpty()) {
		QFile file(path);
		if (file.exists()) {
			auto size = file.size();
			return size;
		}
	}
	return 0;
}

void loadDocuments(not_null<Main::Session*> session, const std::vector<not_null<HistoryItem*>> &items) {
	for (const auto &item : items) {
		if (const auto data = item->media()->document()) {
			const auto size = fileSize(item);

			if (size == data->size) {
				continue;
			}

			loadDocumentSync(session, data, item);
		} else if (auto photo = item->media()->photo()) {
			if (fileSize(item) == photo->imageByteSize(Data::PhotoSize::Large)) {
				continue;
			}

			loadPhotoSync(session, std::pair(photo, item->fullId()));
		}
	}
}

void loadDocumentSync(not_null<Main::Session*> session, DocumentData *data, not_null<HistoryItem*> item) {
	auto latch = std::make_shared<TimedCountDownLatch>(1);
	auto lifetime = std::make_shared<rpl::lifetime>();

	auto path = filePath(session, item->media());
	if (path.isEmpty()) {
		return;
	}
	crl::on_main(session, [=]
	{
		data->save(Data::FileOriginMessage(item->fullId()), path);

		rpl::single() | rpl::then(
			session->downloaderTaskFinished()
		) | rpl::filter([=]
		{
			return !data || data->status == FileDownloadFailed || fileSize(item) == data->size;
		}) | rpl::take(1) | rpl::on_next([=]() mutable
								  {
									  latch->countDown();
								  },
								  *lifetime);
	});

	constexpr auto overall = std::chrono::minutes(15);
	const auto startTime = std::chrono::steady_clock::now();

	while (std::chrono::steady_clock::now() - startTime < overall) {
		if (latch->await(std::chrono::minutes(5))) {
			break;
		}

		if (!data || !data->loading()) {
			break;
		}
	}

	crl::on_main([lifetime = std::move(lifetime)] {
		lifetime->destroy();
	});
}

void forwardMessagesSync(not_null<Main::Session*> session,
						 const std::vector<not_null<HistoryItem*>> &items,
						 const ApiWrap::SendAction &action,
						 Data::ForwardOptions options) {
	auto latch = std::make_shared<TimedCountDownLatch>(1);

	crl::on_main(session, [=]
	{
		session->api().forwardMessages(Data::ResolvedForwardDraft(items, options),
									   action,
									   [=]
									   {
										   latch->countDown();
									   });
	});


	latch->await(std::chrono::minutes(1));
}

void loadPhotoSync(not_null<Main::Session*> session, const std::pair<not_null<PhotoData*>, FullMsgId> &photo) {
	const auto path = pathForSave(session);
	if (path.isEmpty()) {
		return;
	}
	if (!QDir().mkpath(path)) {
		return;
	}

	const auto view = photo.first->createMediaView();
	if (!view) {
		return;
	}
	view->wanted(Data::PhotoSize::Large, photo.second);

	const auto finalCheck = [=]
	{
		return view->loaded();
	};

	const auto saveToFiles = [=]
	{
		const auto fullPath = QDir(path).filePath(
			ForwardPhotoName(photo.first));
		view->saveToFile(fullPath);
	};

	auto latch = std::make_shared<TimedCountDownLatch>(1);
	auto lifetime = std::make_shared<rpl::lifetime>();

	if (finalCheck()) {
		saveToFiles();
	} else {
		crl::on_main(session, [=]
		{
			rpl::single() | rpl::then(
				session->downloaderTaskFinished()
			) | rpl::filter([=]
			{
				return finalCheck();
			}) | rpl::take(1) | rpl::on_next([=]() mutable
									  {
										  saveToFiles();
										  latch->countDown();
									  },
									  *lifetime);
		});
		latch->await(std::chrono::minutes(5));
		crl::on_main([lifetime = std::move(lifetime)] {
			lifetime->destroy();
		});
	}
}

void sendMessageSync(not_null<Main::Session*> session, Api::MessageToSend &&message) {
	const auto action = message.action;
	SendAndWait(session, action, 1, [session, message = std::move(message)]() mutable {
		// we cannot send events to objects
		// owned by a different thread
		// because sendMessage updates UI too

		session->api().sendMessage(std::move(message));
	});
}

void sendDocumentSync(not_null<Main::Session*> session,
					  Ui::PreparedGroup &group,
					  SendMediaType type,
					  TextWithTags &&caption,
					  const Api::SendAction &action) {
	auto groupId = std::make_shared<SendingAlbum>();
	groupId->groupId = base::RandomValue<uint64>();
	const auto count = int(group.list.files.size());

	SendAndWait(session, action, count, [
		session,
		groupId,
		type,
		action,
		lst = std::move(group.list),
		caption = std::move(caption)
	]() mutable {
		auto size = lst.files.size();
		if (!lst.files.empty()) {
			lst.files.front().caption = std::move(caption);
		}
		session->api().sendFiles(
			std::move(lst),
			type,
			size > 1 ? groupId : nullptr,
			action);
	});
}

void sendStickerSync(not_null<Main::Session*> session,
					 Api::MessageToSend &&message,
					 not_null<DocumentData*> document) {
	const auto action = message.action;
	SendAndWait(session, action, 1, [document, message = std::move(message)]() mutable {
		Api::SendExistingDocument(std::move(message), document, std::nullopt);
	});
}

void sendVoiceSync(not_null<Main::Session*> session,
				   const QByteArray &data,
				   int64_t duration,
				   bool video,
				   Api::MessageToSend &&message) {
	const auto action = message.action;

	SendAndWait(session, action, 1, [
		session,
		data,
		duration,
		video,
		action,
		message = std::move(message)
	] {
		const auto to = FileLoadTo(
			action.history->peer->id,
			action.options,
			action.replyTo,
			action.replaceMediaOf);
		session->api().fileLoader()->addTask(std::make_unique<FileLoadTask>(FileLoadTask::VoiceArgs{
			.session = session,
			.voice = data,
			.duration = duration,
			.waveform = QVector<signed char>(),
			.video = video,
			.to = to,
			.caption = message.textWithTags
		}));
	});
}

} // namespace LuxurySync
