// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/features/watch/watched_media.h"

#include "luxury/luxury_settings.h"
#include "luxury/utils/telegram_helpers.h"
#include "data/data_document.h"
// save() and wanted() take a Data::FileOrigin by value, and the FullMsgId that
// converts into one only converts where the type is complete: data_document.h
// gets by with a forward declaration, so it has to be included here.
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <atomic>

namespace LuxuryFeatures::Watch {
namespace {

// Fixed rather than exposed as settings: nothing asks to tune them yet, and a
// knob nobody moves is a row of dead UI. Move them into LuxurySettings the day
// someone actually needs a different number.
//
// One watched channel posting films would otherwise take the connection and the
// disk on its own, so there is both a per-file and a total limit.
constexpr auto kMaxFileSize = int64(64) * 1024 * 1024;
constexpr auto kMaxTotalSize = int64(2) * 1024 * 1024 * 1024;

// A file younger than this may still be being written by a loader we started,
// and its message has had no time to be deleted either. Never pruned.
constexpr auto kPruneGraceSeconds = 60;

// Walking the folder costs a stat per file, so it happens every so many fetches
// instead of on each one.
constexpr auto kPruneEveryFetches = 32;

// Only the extension of a sender-supplied name is used, and only its ASCII
// letters and digits: everything else is a path, a device name or a surprise.
constexpr auto kMaxSuffixLength = 8;

// A photo cannot report completion by itself -- the loader answers on the
// session's downloader signal -- so it waits here until that fires. Bounded,
// because media that never loads would otherwise wait forever.
constexpr auto kMaxAwaitingPhotos = 256;

// Roughly a minute of downloader ticks. A photo still not loaded by then is one
// that is not going to load.
constexpr auto kMaxPhotoWaitTicks = 64;

struct AwaitingPhoto {
	QString path;
	std::shared_ptr<Data::PhotoMedia> media;
	// Which session's data the media belongs to. Kept as an id rather than a
	// pointer because the point of it is to be readable after that session is
	// gone: PhotoMedia::saveToFile() reaches its PhotoData, and ~Session has
	// already freed it by then.
	uint64 sessionId = 0;
	int ticks = 0;
};

std::vector<AwaitingPhoto> AwaitingPhotos;
base::flat_set<uint64> SubscribedSessions;
int FetchesSincePrune = 0;
std::atomic<bool> Pruning = false;
std::atomic<bool> ReportedOverBudget = false;

[[nodiscard]] QString PendingDir() {
	return cWorkingDir() + u"tdata/luxury_media/pending/"_q;
}

[[nodiscard]] QString KeptDir() {
	return cWorkingDir() + u"tdata/luxury_media/kept/"_q;
}

[[nodiscard]] QString keptFilePath(const QString &fileName) {
	// Every name here was written by FileName() below, but they come back off a
	// directory anyone can drop a file into, so they are treated as untrusted:
	// anything with a separator in it is not a name this ever wrote.
	if (fileName.isEmpty()
		|| fileName.contains('/')
		|| fileName.contains('\\')
		|| fileName.contains(':')
		|| fileName.startsWith('.')) {
		return QString();
	}
	return KeptDir() + fileName;
}

[[nodiscard]] QString SafeSuffix(
		const QString &name,
		const QString &fallback) {
	auto result = QString();
	for (const auto ch : QFileInfo(name).suffix()) {
		if (result.size() >= kMaxSuffixLength) {
			break;
		} else if (ch.unicode() < 128 && ch.isLetterOrNumber()) {
			result.append(ch.toLower());
		}
	}
	return result.isEmpty() ? fallback : result;
}

[[nodiscard]] QString FileName(
		ID dialogId,
		MsgId messageId,
		uint64 mediaId,
		const QString &suffix) {
	// dialogId is negative for chats and channels, which is fine in a name: it is
	// the same key the deleted-message rows use, and that is what makes a kept
	// file findable from either side.
	return u"%1_%2_%3.%4"_q
		.arg(dialogId)
		.arg(messageId.bare)
		.arg(mediaId)
		.arg(suffix);
}

// Both the fetch and the keep derive the name from the item, so neither has to
// remember anything between them: the folder is the index. Nothing to migrate,
// nothing to get out of sync with the disk.
[[nodiscard]] QString NameForItem(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return QString();
	}
	const auto dialogId = getDialogIdFromPeer(item->history()->peer);
	if (const auto document = media->document()) {
		return FileName(
			dialogId,
			item->id,
			document->id,
			SafeSuffix(document->filename(), u"bin"_q));
	} else if (const auto photo = media->photo()) {
		return FileName(dialogId, item->id, photo->id, u"jpg"_q);
	}
	return QString();
}

void PruneOldFiles() {
	if (Pruning.exchange(true)) {
		return;
	}
	crl::async([pending = PendingDir(), kept = KeptDir()] {
		auto keptSize = int64(0);
		for (const auto &entry : QDir(kept).entryInfoList(QDir::Files)) {
			keptSize += entry.size();
		}
		// Oldest first: QDir::Time sorts newest first and Reversed flips it.
		const auto files = QDir(pending).entryInfoList(
			QDir::Files,
			QDir::Time | QDir::Reversed);
		auto total = keptSize;
		for (const auto &entry : files) {
			total += entry.size();
		}
		const auto now = QDateTime::currentDateTime();
		for (const auto &entry : files) {
			if (total <= kMaxTotalSize) {
				break;
			} else if (entry.lastModified().secsTo(now) < kPruneGraceSeconds) {
				// The list is oldest first, so everything left is younger still.
				break;
			} else if (QFile::remove(entry.absoluteFilePath())) {
				total -= entry.size();
			}
		}
		if (keptSize > kMaxTotalSize && !ReportedOverBudget.exchange(true)) {
			// Kept files belong to messages that are already gone, so pruning
			// them would delete the thing this exists to save. Nothing here can
			// free them; the watched-chats list is where the user gets to.
			LOG(("Luxury Watch: kept media is %1 MB, over the %2 MB budget."
				).arg(keptSize / (1024 * 1024)
				).arg(kMaxTotalSize / (1024 * 1024)));
		}
		Pruning = false;
	});
}

void FlushAwaitingPhotos() {
	for (auto i = AwaitingPhotos.begin(); i != AwaitingPhotos.end();) {
		if (i->media->loaded()) {
			// Writes the video content of a live photo and the large image
			// otherwise -- exactly the pair wanted() asked for.
			if (!i->media->saveToFile(i->path)) {
				DEBUG_LOG(("Luxury Watch: could not write %1").arg(i->path));
			}
			i = AwaitingPhotos.erase(i);
		} else if (++i->ticks > kMaxPhotoWaitTicks) {
			i = AwaitingPhotos.erase(i);
		} else {
			++i;
		}
	}
}

void DropAwaitingPhotos(uint64 sessionId) {
	for (auto i = AwaitingPhotos.begin(); i != AwaitingPhotos.end();) {
		i = (i->sessionId == sessionId)
			? AwaitingPhotos.erase(i)
			: (i + 1);
	}
}

void SubscribeToDownloads(not_null<Main::Session*> session) {
	const auto id = session->uniqueId();
	if (!SubscribedSessions.emplace(id).second) {
		return;
	}
	session->downloaderTaskFinished(
	) | rpl::start_with_next([] {
		FlushAwaitingPhotos();
	}, session->lifetime());

	// The same account logged in again is a new Session with the same id, so the
	// mark has to come off with the old one or the new one never subscribes.
	//
	// The waiting photos go with it too. ~Session has already run data().clear()
	// by the time this fires, so their PhotoData is freed, and saveToFile() would
	// read it -- a photo that outlived its account is one nothing may ask about
	// again. Destroying the media itself is safe: ~PhotoMedia touches nothing.
	session->lifetime().add([id] {
		SubscribedSessions.remove(id);
		DropAwaitingPhotos(id);
	});
}

} // namespace

void processNewMessage(not_null<HistoryItem*> item) {
	const auto &settings = LuxurySettings::getInstance();
	if (!settings.watchAnything()) {
		return;
	}
	const auto history = item->history();
	if (!settings.isWatched(getDialogIdFromPeer(history->peer))) {
		return;
	}
	const auto media = item->media();
	if (!media) {
		return;
	}
	const auto name = NameForItem(item);
	if (name.isEmpty()) {
		return;
	}
	const auto path = PendingDir() + name;
	if (QFile::exists(path) || QFile::exists(KeptDir() + name)) {
		// Already fetched, or already kept for a delete we have seen: either way
		// the bytes are on disk and there is nothing left to ask for.
		return;
	}
	const auto origin = item->fullId();
	if (const auto document = media->document()) {
		if (document->size > kMaxFileSize) {
			DEBUG_LOG(("Luxury Watch: %1 is %2 bytes, over the file limit."
				).arg(name).arg(document->size));
			return;
		} else if (document->loading()) {
			// Something already asked for this one. save() below would try to
			// point that loader at our path, fail, and cancel it.
			return;
		} else if (!QDir().mkpath(PendingDir())) {
			LOG(("Luxury Watch: could not create %1").arg(PendingDir()));
			return;
		}
		// Writes straight to our path with no save dialog, and copies the bytes
		// when the file is already downloaded. Not gated on the auto-download
		// settings on purpose: watching a chat is the explicit request that
		// overrides them.
		//
		// It also makes our path the document's known location, so a prune can
		// leave tdesktop pointing at a file that is gone -- which it handles by
		// downloading again, the same as any file the user moved.
		document->save(origin, path, LoadFromCloudOrLocal, true);
	} else if (const auto photo = media->photo()) {
		if (AwaitingPhotos.size() >= kMaxAwaitingPhotos) {
			return;
		} else if (!QDir().mkpath(PendingDir())) {
			LOG(("Luxury Watch: could not create %1").arg(PendingDir()));
			return;
		}
		auto view = photo->createMediaView();
		view->wanted(Data::PhotoSize::Large, origin);
		const auto session = &history->session();
		AwaitingPhotos.push_back({
			path,
			std::move(view),
			session->uniqueId(),
		});
		SubscribeToDownloads(session);
	} else {
		return;
	}
	if (++FetchesSincePrune >= kPruneEveryFetches) {
		FetchesSincePrune = 0;
		PruneOldFiles();
	}
}

QString keepMediaForDeleted(not_null<HistoryItem*> item) {
	// Not gated on isWatched(): a chat can stop being watched between the fetch
	// and the delete, and the file is already on disk by then. Throwing it away
	// at that point would be the opposite of the point.
	const auto name = NameForItem(item);
	if (name.isEmpty()) {
		return QString();
	} else if (QFile::exists(KeptDir() + name)) {
		// Deleted twice, or kept by an earlier run. Either way it is already safe.
		return name;
	} else if (!QFile::exists(PendingDir() + name)) {
		return QString();
	} else if (!QDir().mkpath(KeptDir())) {
		LOG(("Luxury Watch: could not create %1").arg(KeptDir()));
		return QString();
	}
	// A rename inside one directory tree, so no bytes move and this stays cheap
	// enough for the hundreds of items a bulk delete posts at once.
	return QFile::rename(PendingDir() + name, KeptDir() + name)
		? name
		: QString();
}

QString keptFileForMessage(ID dialogId, MsgId messageId) {
	// The media id is the one part of the name the viewer cannot know, so the
	// first two thirds are matched and the rest globbed. No ambiguity from the
	// trailing separator: "5_45_*" cannot match "5_456_7.jpg".
	const auto prefix = u"%1_%2_"_q.arg(dialogId).arg(messageId.bare);
	const auto names = QDir(KeptDir()).entryList(
		{ prefix + '*' },
		QDir::Files);
	return names.isEmpty() ? QString() : keptFilePath(names.front());
}

} // namespace LuxuryFeatures::Watch
