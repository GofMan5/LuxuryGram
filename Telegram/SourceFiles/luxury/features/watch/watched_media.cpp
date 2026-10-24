// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/features/watch/watched_media.h"

#include "luxury/luxury_settings.h"
#include "luxury/utils/telegram_helpers.h"
#include "base/call_delayed.h"
#include "data/data_document.h"
// save() and wanted() take a Data::FileOrigin by value, and the FullMsgId that
// converts into one only converts where the type is complete: data_document.h
// gets by with a forward declaration, so it has to be included here.
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang_auto.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <atomic>
#include <map>
#include <mutex>
#include <utility>

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

// A photo still not loaded by then is one that is not going to load. Measured in
// time rather than in downloader ticks: an active chat fires those several times
// a second, which used to drop waiting photos after a few seconds.
constexpr auto kPhotoWaitTimeout = crl::time(60 * 1000);

// How long a keep waits for the loader that still owns its file. Longer than the
// photo timeout because this file is the whole point: its message is gone.
constexpr auto kKeepWaitTimeout = crl::time(30 * 60 * 1000);

// Bounded like the photo queue: media that never finishes must not accumulate.
constexpr auto kMaxPendingKeeps = 256;

// A deferred keep is normally retried by the next finished download. But the
// message that was deleted may have been the last transfer in flight, and then
// nothing in an idle app fires again -- and the file waits in pending/ until
// PruneOldFiles() takes it. Slow on purpose: this only covers that case.
constexpr auto kKeepRetryDelay = crl::time(30 * 1000);

// Give-ups logged per file would never reach the user, and a failing disk
// drops whole batches at once. They are counted into one window and surface
// as a single toast when it closes.
constexpr auto kGiveUpToastDelay = crl::time(60 * 1000);

struct MediaScope {
	uint64 sessionId = 0;
	ID dialogId = 0;
	QString nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
	std::atomic<bool> active = true;
	std::atomic<bool> photoWritesAllowed = true;
};

std::map<std::pair<uint64, ID>, std::shared_ptr<MediaScope>> Scopes;
std::mutex MediaFilesMutex;
base::flat_set<QString> ClearingPaths;

[[nodiscard]] std::shared_ptr<MediaScope> ScopeForItem(
		not_null<HistoryItem*> item) {
	const auto key = std::pair(
		item->history()->session().uniqueId(),
		ID(item->history()->peer->id.value));
	auto &scope = Scopes[key];
	if (!scope) {
		scope = std::make_shared<MediaScope>();
		scope->sessionId = key.first;
		scope->dialogId = key.second;
	}
	return scope;
}

struct AwaitingDocument {
	uint64 sessionId = 0;
	uint64 documentId = 0;
	QString path;
	std::shared_ptr<MediaScope> scope;
};

struct AwaitingPhoto {
	QString path;
	std::shared_ptr<MediaScope> scope;
	std::shared_ptr<Data::PhotoMedia> media;
	// Which session's data the media belongs to. Kept as an id rather than a
	// pointer because the point of it is to be readable after that session is
	// gone: PhotoMedia::saveToFile() reaches its PhotoData, and ~Session has
	// already freed it by then.
	uint64 sessionId = 0;
	crl::time deadline = 0;
	Fn<void()> cancelLoad;
};

// A file whose message was deleted while a loader we started was still writing
// it. Its scope survives logout, but an explicit clear invalidates it.
struct PendingKeep {
	QString name;
	std::shared_ptr<MediaScope> scope;
	crl::time deadline = 0;
};

std::vector<AwaitingDocument> AwaitingDocuments;
std::vector<AwaitingPhoto> AwaitingPhotos;
std::vector<PendingKeep> PendingKeeps;
std::map<QString, std::shared_ptr<MediaScope>> PhotoWrites;

void FlushPendingKeeps();
// One retry timer at a time: anything pushed while it is armed rides on it.
bool PendingKeepsRetrying = false;
uint64 RetryGeneration = 0;
// Give-ups counted while a toast window is open; the delayed toast swaps
// the count back to zero when it fires.
int GivenUpKeeps = 0;
crl::time GivenUpKeepsWindow = 0;
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
		|| fileName.startsWith('.')
		|| ClearingPaths.contains(KeptDir() + fileName)) {
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
		const std::shared_ptr<MediaScope> &scope,
		ID dialogId,
		MsgId messageId,
		uint64 mediaId,
		const QString &suffix) {
	// dialogId is negative for chats and channels, which is fine in a name: it is
	// the same key the deleted-message rows use, and that is what makes a kept
	// file findable from either side.
	const auto prefix = u"v2_%1_%2_%3_%4_"_q
		.arg(scope->sessionId)
		.arg(dialogId)
		.arg(messageId.bare)
		.arg(mediaId);
	for (const auto &directory : { KeptDir(), PendingDir() }) {
		for (const auto &name : QDir(directory).entryList(
				{ prefix + '*' }, QDir::Files)) {
			if (!ClearingPaths.contains(directory + name)) {
				return name;
			}
		}
	}
	return prefix + scope->nonce + '.' + suffix;
}

// The folder remains the durable index. v2 names include the account and a
// clear generation; ambiguous legacy names are preserved but never auto-adopted.
[[nodiscard]] QString NameForItem(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return QString();
	}
	const auto scope = ScopeForItem(item);
	const auto dialogId = scope->dialogId;
	if (const auto document = media->document()) {
		return FileName(
			scope,
			dialogId,
			item->id,
			document->id,
			SafeSuffix(document->filename(), u"bin"_q));
	} else if (const auto photo = media->photo()) {
		return FileName(scope, dialogId, item->id, photo->id, u"jpg"_q);
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
			{ u"v2_*"_q },
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
		if (keptSize > kMaxTotalSize) {
			// Kept files belong to messages that are already gone, so pruning
			// them would delete the thing this exists to save. Nothing here can
			// free them; the watched-chats list is where the user gets to.
			if (!ReportedOverBudget.exchange(true)) {
				LOG(("Luxury Watch: kept media is %1 MB, over the %2 MB budget."
					).arg(keptSize / (1024 * 1024)
					).arg(kMaxTotalSize / (1024 * 1024)));
			}
		} else {
			// Said once per time it goes over, not once per process: the user
			// can clear kept media and would never be told again otherwise.
			ReportedOverBudget = false;
		}
		Pruning = false;
	});
}

// PhotoMedia and its PhotoData are main-thread only, so the bytes come out here
// and only the write goes to a pool thread. A large photo is megabytes and this
// runs inside a downloader tick with up to kMaxAwaitingPhotos of them queued.
void SavePhotoAsync(
		const std::shared_ptr<Data::PhotoMedia> &media,
		const QString &path,
		const std::shared_ptr<MediaScope> &scope) {
	if (PhotoWrites.contains(path)) return;
	const auto finished = [path, scope] {
		crl::on_main([path, scope] {
			const auto i = PhotoWrites.find(path);
			if (i != PhotoWrites.end() && i->second == scope) PhotoWrites.erase(i);
			FlushPendingKeeps();
		});
	};
	constexpr auto large = Data::PhotoSize::Large;
	// Writes the video content of a live photo and the large image otherwise --
	// exactly the pair wanted() asked for.
	auto bytes = media->videoContent(large);
	if (bytes.isEmpty()) {
		bytes = media->imageBytes(large);
	}
	if (!bytes.isEmpty()) {
		PhotoWrites.emplace(path, scope);
		crl::async([path, scope, finished, bytes = std::move(bytes)] {
			const auto notify = gsl::finally(finished);
			const auto lock = std::lock_guard(MediaFilesMutex);
			if (!scope->active || !scope->photoWritesAllowed) return;
			auto f = QFile(path);
			if (!f.open(QIODevice::WriteOnly)
				|| f.write(bytes) != bytes.size()) {
				DEBUG_LOG(("Luxury Watch: could not write %1").arg(path));
			}
		});
		return;
	}
	// No stored bytes, only a decoded frame. QImage is copy-on-write and save()
	// is const, so the pool thread reads the same buffer without detaching it.
	auto image = media->image(large)->original();
	if (image.isNull()) {
		DEBUG_LOG(("Luxury Watch: nothing to write for %1").arg(path));
		return;
	}
	PhotoWrites.emplace(path, scope);
	crl::async([path, scope, finished, image = std::move(image)] {
		const auto notify = gsl::finally(finished);
		const auto lock = std::lock_guard(MediaFilesMutex);
		if (!scope->active || !scope->photoWritesAllowed) return;
		if (!image.save(path, "JPG")) {
			DEBUG_LOG(("Luxury Watch: could not write %1").arg(path));
		}
	});
}

void CancelUnusedPhotoLoads(const std::vector<AwaitingPhoto> &photos) {
	base::flat_set<const Data::PhotoMedia*> canceled;
	for (const auto &entry : photos) {
		if (!entry.cancelLoad || !getSessionByUniqueId(entry.sessionId)) continue;
		auto sharedWithWatch = false;
		for (auto &remaining : AwaitingPhotos) {
			if (remaining.media == entry.media) {
				if (!remaining.cancelLoad) remaining.cancelLoad = entry.cancelLoad;
				sharedWithWatch = true;
			}
		}
		const auto heldByWatch = std::count_if(photos.begin(), photos.end(), [&](const auto &other) {
			return other.media == entry.media;
		});
		// A later user/media-view owner may have joined the same loader.
		if (!sharedWithWatch && entry.media.use_count() == heldByWatch
			&& canceled.emplace(entry.media.get()).second) {
			entry.cancelLoad();
		}
	}
}

void FlushAwaitingPhotos() {
	// Indexed rather than iterated: SavePhotoAsync() reaches PhotoData, and an
	// upstream change there that reaches back into the downloader would re-enter
	// this and invalidate a live iterator.
	const auto now = crl::now();
	auto kept = std::vector<AwaitingPhoto>();
	auto expired = std::vector<AwaitingPhoto>();
	auto pending = base::take(AwaitingPhotos);
	kept.reserve(pending.size());
	for (auto &entry : pending) {
		if (!entry.scope->active) {
			continue;
		} else if (entry.media->loaded()) {
			SavePhotoAsync(entry.media, entry.path, entry.scope);
		} else if (now < entry.deadline) {
			kept.push_back(std::move(entry));
		} else {
			expired.push_back(std::move(entry));
		}
	}
	// Anything queued while the writes were being started stays queued.
	AwaitingPhotos.insert(
		AwaitingPhotos.end(),
		std::make_move_iterator(kept.begin()),
		std::make_move_iterator(kept.end()));
	CancelUnusedPhotoLoads(expired);
}

void DropAwaitingPhotos(uint64 sessionId) {
	for (auto i = AwaitingPhotos.begin(); i != AwaitingPhotos.end();) {
		i = (i->sessionId == sessionId)
			? AwaitingPhotos.erase(i)
			: (i + 1);
	}
}

[[nodiscard]] bool MoveToKept(const QString &name) {
	if (!QDir().mkpath(KeptDir())) {
		LOG(("Luxury Watch: could not create %1").arg(KeptDir()));
		return false;
	}
	// A rename inside one directory tree, so no bytes move and this stays cheap
	// enough for the hundreds of items a bulk delete posts at once.
	return QFile::rename(PendingDir() + name, KeptDir() + name);
}

[[nodiscard]] bool MediaWritePending(const QString &path) {
	return PhotoWrites.contains(path)
		|| ranges::any_of(AwaitingPhotos, [&](const auto &photo) {
			return photo.scope->active && photo.path == path;
		})
		|| ranges::any_of(AwaitingDocuments, [&](const auto &document) {
			const auto session = getSessionByUniqueId(document.sessionId);
			return session && document.scope->active && document.path == path
				&& session->data().document(document.documentId)->loadingFilePath() == path;
		});
}

void RetryPendingKeepsLater() {
	if (PendingKeepsRetrying || PendingKeeps.empty()) {
		return;
	}
	PendingKeepsRetrying = true;
	const auto generation = RetryGeneration;
	base::call_delayed(kKeepRetryDelay, [generation] {
		if (generation != RetryGeneration) return;
		PendingKeepsRetrying = false;
		FlushPendingKeeps();
	});
}

void ShowGivenUpKeepsToast() {
	const auto count = std::exchange(GivenUpKeeps, 0);
	GivenUpKeepsWindow = 0;
	Ui::Toast::Show(
		tr::luxury_WatcherMediaSaveFailed(tr::now, lt_count, count));
}

void FlushPendingKeeps() {
	if (PendingKeeps.empty()) {
		return;
	}
	const auto now = crl::now();
	auto retry = std::vector<PendingKeep>();
	auto pending = base::take(PendingKeeps);
	retry.reserve(pending.size());
	for (auto &entry : pending) {
		const auto path = PendingDir() + entry.name;
		const auto writing = MediaWritePending(path);
		if (!entry.scope->active) {
			continue;
		} else if (QFile::exists(KeptDir() + entry.name)) {
			continue;
		} else if (!writing && !QFile::exists(path)) {
			// Pruned, or the loader was cancelled and removed it. Nothing left.
			continue;
		} else if (!writing && MoveToKept(entry.name)) {
			continue;
		} else if (now < entry.deadline) {
			// The loader still holds the file open. PruneOldFiles() leaves files
			// younger than kPruneGraceSeconds alone, and every completion gives
			// another attempt.
			retry.push_back(std::move(entry));
		} else {
			LOG(("Luxury Watch: gave up moving %1 to kept.").arg(entry.name));
			++GivenUpKeeps;
			if (!GivenUpKeepsWindow) {
				GivenUpKeepsWindow = now;
				base::call_delayed(kGiveUpToastDelay, [] {
					ShowGivenUpKeepsToast();
				});
			}
		}
	}
	PendingKeeps.insert(
		PendingKeeps.end(),
		std::make_move_iterator(retry.begin()),
		std::make_move_iterator(retry.end()));
	RetryPendingKeepsLater();
}

void SubscribeToDownloads(not_null<Main::Session*> session) {
	const auto id = session->uniqueId();
	if (!SubscribedSessions.emplace(id).second) {
		return;
	}
	session->downloaderTaskFinished(
	) | rpl::on_next([] {
		std::erase_if(AwaitingDocuments, [](const AwaitingDocument &entry) {
			const auto owner = getSessionByUniqueId(entry.sessionId);
			return !owner || !entry.scope->active
				|| owner->data().document(entry.documentId)->loadingFilePath() != entry.path;
		});
		FlushAwaitingPhotos();
		FlushPendingKeeps();
	}, session->lifetime());

	// The same account logged in again is a new Session with the same id, so the
	// mark has to come off with the old one or the new one never subscribes.
	//
	// The waiting photos go with it too. ~Session has already run data().clear()
	// by the time this fires, so their PhotoData is freed, and saveToFile() would
	// read it -- a photo that outlived its account is one nothing may ask about
	// again. Destroying the media itself is safe: ~PhotoMedia touches nothing.
	session->data().sessionDataAboutToBeCleared() | rpl::on_next([id] {
		DropAwaitingPhotos(id);
		std::erase_if(AwaitingDocuments, [id](const AwaitingDocument &entry) {
			return entry.sessionId == id;
		});
		std::erase_if(Scopes, [id](const auto &entry) {
			if (entry.first.first != id) return false;
			entry.second->photoWritesAllowed = false;
			return true;
		});
	}, session->lifetime());
	session->lifetime().add([id] {
		SubscribedSessions.remove(id);
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
	std::erase_if(AwaitingDocuments, [](const AwaitingDocument &entry) {
		const auto session = getSessionByUniqueId(entry.sessionId);
		return !session || !entry.scope->active
			|| session->data().document(entry.documentId)->loadingFilePath() != entry.path;
	});
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
		if (document->saveToCache()) {
			// Stickers, custom emoji, small GIFs, voice notes, wallpapers and
			// themes. tdesktop keeps its own copy of these and resolves them
			// through it, and save() below would additionally make our prunable
			// path their known file location -- persisted through
			// writeFileLocation(), so it survives a restart. A prune then leaves
			// Sticker::setupPlayer() building a player from a path that is gone,
			// and the sticker never draws again. Nothing is lost by skipping
			// them: the copy that outlives the message is tdesktop's.
			return;
		} else if (document->size > kMaxFileSize) {
			DEBUG_LOG(("Luxury Watch: %1 is %2 bytes, over the file limit."
				).arg(name).arg(document->size));
			return;
		} else if (AwaitingDocuments.size() >= kMaxPendingKeeps) {
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
		SubscribeToDownloads(&history->session());
		AwaitingDocuments.push_back({
			history->session().uniqueId(),
			document->id,
			path,
			ScopeForItem(item),
		});
		document->save(origin, path, LoadFromCloudOrLocal, true);
		std::erase_if(AwaitingDocuments, [&](const AwaitingDocument &entry) {
			return entry.sessionId == history->session().uniqueId()
				&& entry.documentId == document->id
				&& document->loadingFilePath() != entry.path;
		});

	} else if (const auto photo = media->photo()) {
		if (AwaitingPhotos.size() + PhotoWrites.size() >= kMaxAwaitingPhotos) {
			return;
		} else if (!QDir().mkpath(PendingDir())) {
			LOG(("Luxury Watch: could not create %1").arg(PendingDir()));
			return;
		}
		auto view = photo->createMediaView();
		const auto ownsLoad = !photo->loading() && !view->loaded();
		view->wanted(Data::PhotoSize::Large, origin);
		const auto session = &history->session();
		AwaitingPhotos.push_back({
			path,
			ScopeForItem(item),
			std::move(view),
			session->uniqueId(),
			crl::now() + kPhotoWaitTimeout,
			ownsLoad ? photo->cancelLoadCallback(Data::PhotoSize::Large) : Fn<void()>(),
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
	}
	const auto path = PendingDir() + name;
	if (MediaWritePending(path)) {
		// The loader still owns the file. Renaming it now fails outright on
		// Windows, and on Linux moves the inode out from under the loader, which
		// then reports a location that no longer holds what it says. Wait for a
		// completion instead -- this is the case the feature exists for, a post
		// deleted seconds after it arrived, so it must not be dropped.
		if (PendingKeeps.size() < kMaxPendingKeeps
			&& !ranges::contains(PendingKeeps, name, &PendingKeep::name)) {
			PendingKeeps.push_back({ name, ScopeForItem(item), crl::now() + kKeepWaitTimeout });
			RetryPendingKeepsLater();
		}
		return QString();
	}
	return QFile::exists(path) && MoveToKept(name) ? name : QString();
}

QString keptFileForMessage(uint64 sessionId, ID dialogId, MsgId messageId) {
	// Match the account and original message, not the viewer's synthetic id.
	// The trailing separator prevents 45 from matching message 456.
	const auto prefix = u"v2_%1_%2_%3_"_q.arg(sessionId).arg(dialogId).arg(messageId.bare);
	const auto names = QDir(KeptDir()).entryList(
		{ prefix + '*' },
		QDir::Files);
	for (const auto &name : names) {
		if (const auto path = keptFilePath(name); !path.isEmpty()) {
			return path;
		}
	}
	return QString();
}

bool ownsFetchedPath(const QString &path) {
	// PendingDir() only: a kept file was already written and is never a loader's
	// target, and the parent tdata/ holds the temp download directory, which is
	// where a user-chosen "Temporary folder" download really lands.
	return path.startsWith(PendingDir());
}

namespace {

void ClearMedia(std::optional<std::pair<uint64, ID>> selected) {
	const auto matches = [&](const auto &scope) {
		return !selected || *selected == std::pair(scope->sessionId, scope->dialogId);
	};
	{
		// A photo write that already started completes before the snapshot below;
		// queued writes observe the inactive scope and cannot recreate its file.
		const auto lock = std::lock_guard(MediaFilesMutex);
		std::erase_if(Scopes, [&](const auto &entry) {
			if (!matches(entry.second)) return false;
			entry.second->active = false;
			return true;
		});
		std::erase_if(PhotoWrites, [&](const auto &entry) {
			if (!matches(entry.second)) return false;
			entry.second->active = false;
			return true;
		});
		for (auto &entry : PendingKeeps) {
			if (matches(entry.scope)) entry.scope->active = false;
		}
		for (auto &entry : AwaitingPhotos) {
			if (matches(entry.scope)) entry.scope->active = false;
		}
		for (auto &entry : AwaitingDocuments) {
			if (matches(entry.scope)) entry.scope->active = false;
		}
	}
	std::erase_if(PendingKeeps, [&](const auto &entry) { return matches(entry.scope); });
	auto photos = base::take(AwaitingPhotos);
	std::erase_if(photos, [&](auto &entry) {
		if (matches(entry.scope)) return false;
		AwaitingPhotos.push_back(std::move(entry));
		return true;
	});
	const auto documents = base::take(AwaitingDocuments);
	for (const auto &entry : documents) {
		if (!matches(entry.scope)) AwaitingDocuments.push_back(entry);
	}
	++RetryGeneration;
	PendingKeepsRetrying = false;
	RetryPendingKeepsLater();

	const auto prefix = selected
		? u"v2_%1_%2_"_q.arg(selected->first).arg(selected->second)
		: u"v2_"_q;
	auto paths = QStringList();
	for (const auto &directory : { PendingDir(), KeptDir() }) {
		for (const auto &entry : QDir(directory).entryInfoList(
				{ prefix + '*' }, QDir::Files)) {
			const auto path = entry.absoluteFilePath();
			if (ClearingPaths.emplace(path).second) paths.push_back(path);
		}
	}
	// cancel() synchronously notifies the downloader. All old queues and scopes
	// have been detached first, so that notification may safely start new work.
	for (const auto &entry : documents) {
		if (!matches(entry.scope)) continue;
		if (const auto session = getSessionByUniqueId(entry.sessionId)) {
			const auto document = session->data().document(entry.documentId);
			if (document->loadingFilePath() == entry.path) document->cancel();
		}
	}
	CancelUnusedPhotoLoads(photos);
	crl::async([paths = std::move(paths)] {
		{
			const auto lock = std::lock_guard(MediaFilesMutex);
			for (const auto &path : paths) {
				if (QFile::exists(path) && !QFile::remove(path)) {
					LOG(("Luxury Watch: could not clear %1").arg(path));
				}
			}
		}
		crl::on_main([paths] {
			for (const auto &path : paths) ClearingPaths.remove(path);
			ReportedOverBudget = false;
		});
	});
}

} // namespace

void clearKeptMedia() {
	ClearMedia(std::nullopt);
}

void clearKeptMediaForDialog(uint64 sessionId, ID dialogId) {
	ClearMedia(std::pair(sessionId, dialogId));
}

} // namespace LuxuryFeatures::Watch
