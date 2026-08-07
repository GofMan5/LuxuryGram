// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/features/forward/luxury_forward.h"

#include "apiwrap.h"
#include "lang_auto.h"
#include "luxury/features/forward/luxury_sync.h"
#include "luxury/utils/telegram_helpers.h"
#include "base/base_file_utilities.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "storage/localimageloader.h"
#include "storage/storage_account.h"
#include "storage/storage_media_prepare.h"
#include "styles/style_boxes.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/text/text_utilities.h"

#include <unordered_map>

namespace LuxuryForward {

namespace {

std::mutex ForwardStatesMutex;
std::unordered_map<
	const Main::Session*,
	std::unordered_map<PeerId, std::shared_ptr<ForwardState>>> ForwardStates;
constexpr auto kMaxVoiceBytes = 64 * 1024 * 1024;

struct ForwardItem {
	FullMsgId id;
	MessageGroupId groupId;
	TextWithTags text;
	QString path;
	QString displayName;
	qint64 expectedSize = 0;
	int64 duration = 0;
	bool luxury = false;
	bool downloadable = false;
	bool photo = false;
	bool document = false;
	bool sticker = false;
	bool voice = false;
	bool round = false;
	bool video = false;
	bool invertCaption = false;
};

struct ForwardChunk {
	bool luxury = false;
	std::vector<ForwardItem> items;
};

struct ForwardJob {
	LuxurySync::WeakSession session;
	const Main::Session *sessionKey = nullptr;
	Api::SendAction action;
	PeerId peerId;
	Data::ForwardOptions options = Data::ForwardOptions::PreserveInfo;
	bool slowmodeApplied = false;
};

ForwardItem SnapshotItem(
		not_null<Main::Session*> session,
		not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	const auto photo = media ? media->photo() : nullptr;
	auto result = ForwardItem();
	result.id = item->fullId();
	result.groupId = item->groupId();
	result.text = extractText(item);
	result.path = LuxurySync::filePath(session, media);
	result.displayName = document
		? base::FileNameFromUserString(document->filename())
		: QString();
	result.expectedSize = document
		? document->size
		: photo
		? photo->imageByteSize(Data::PhotoSize::Large)
		: 0;
	result.duration = document ? document->duration() : 0;
	result.luxury = isLuxuryForwardNeeded(item);
	result.downloadable = mediaDownloadable(media);
	result.photo = (photo != nullptr);
	result.document = (document != nullptr);
	result.sticker = document && document->sticker();
	result.voice = document && document->isVoiceMessage();
	result.round = document && document->isVideoMessage();
	result.video = document
		&& (document->isVideoFile() || document->isGifv());
	result.invertCaption = item->invertMedia();
	return result;
}

std::vector<ForwardItem> SnapshotItems(
		not_null<Main::Session*> session,
		const std::vector<not_null<HistoryItem*>> &items) {
	auto result = std::vector<ForwardItem>();
	result.reserve(items.size());
	for (const auto item : items) {
		result.push_back(SnapshotItem(session, item));
	}
	return result;
}

ForwardJob SnapshotJob(
		not_null<Main::Session*> session,
		const Api::SendAction &action,
		Data::ForwardOptions options) {
	return ForwardJob{
		.session = base::make_weak(session),
		.sessionKey = session,
		.action = action,
		.peerId = action.history->peer->id,
		.options = options,
		.slowmodeApplied = action.history->peer->slowmodeApplied(),
	};
}

std::shared_ptr<ForwardState> FindForwardState(
		PeerId id,
		const Main::Session *session) {
	const auto lock = std::lock_guard(ForwardStatesMutex);
	const auto sessionIt = ForwardStates.find(session);
	if (sessionIt == end(ForwardStates)) {
		return nullptr;
	}
	const auto i = sessionIt->second.find(id);
	return (i != end(sessionIt->second)) ? i->second : nullptr;
}

void SetForwardState(
		PeerId id,
		const Main::Session *session,
		std::shared_ptr<ForwardState> state) {
	const auto lock = std::lock_guard(ForwardStatesMutex);
	auto &states = ForwardStates[session];
	if (const auto i = states.find(id);
		i != end(states) && i->second != state) {
		i->second->requestStop();
	}
	states[id] = std::move(state);
}

void RemoveForwardState(
		PeerId id,
		const Main::Session *session,
		const std::shared_ptr<ForwardState> &state) {
	const auto lock = std::lock_guard(ForwardStatesMutex);
	const auto sessionIt = ForwardStates.find(session);
	if (sessionIt == end(ForwardStates)) {
		return;
	}
	auto &states = sessionIt->second;
	const auto i = states.find(id);
	if (i != end(states) && i->second == state) {
		states.erase(i);
		if (states.empty()) {
			ForwardStates.erase(sessionIt);
		}
	}
}

void FinishForward(
		PeerId id,
		const std::shared_ptr<ForwardState> &state,
		LuxurySync::WeakSession session,
		const Main::Session *sessionKey) {
	state->updateBottomBar(session, id, ForwardState::State::Finished);
	RemoveForwardState(id, sessionKey, state);
}

} // namespace

bool isForwarding(const PeerId &id, const Main::Session &session) {
	const auto state = id.value ? FindForwardState(id, &session) : nullptr;
	if (!state) {
		return false;
	}
	const auto snapshot = state->snapshot();
	return snapshot.state != ForwardState::State::Finished
		&& snapshot.currentChunk < snapshot.totalChunks
		&& !snapshot.stopRequested;
}

void cancelForward(const PeerId &id, const Main::Session &session) {
	if (const auto state = FindForwardState(id, &session)) {
		state->requestStop();
		FinishForward(id, state, base::make_weak(&session), &session);
	}
}

std::pair<QString, QString> stateName(
		const PeerId &id,
		const Main::Session &session) {
	const auto state = FindForwardState(id, &session);
	if (!state) {
		return std::make_pair(QString(), QString());
	}
	const auto snapshot = state->snapshot();

	QString messagesString = tr::luxury_LuxuryForwardStatusSentCount(tr::now,
														   lt_count1,
														   QString::number(snapshot.sentMessages),
														   lt_count2,
														   QString::number(snapshot.totalMessages)

	);

	QString chunkString = tr::luxury_LuxuryForwardStatusChunkCount(tr::now,
													 lt_count1,
													 QString::number(snapshot.currentChunk + 1),
													 lt_count2,
													 QString::number(snapshot.totalChunks)

	);

	const auto partString = snapshot.totalChunks <= 1
		? messagesString
		: (messagesString + " • " + chunkString);

	QString status;

	if (snapshot.state == ForwardState::State::Preparing) {
		status = tr::luxury_LuxuryForwardStatusPreparing(tr::now);
	} else if (snapshot.state == ForwardState::State::Downloading) {
		return std::make_pair(tr::luxury_LuxuryForwardStatusLoadingMedia(tr::now), "");
	} else if (snapshot.state == ForwardState::State::Sending) {
		status = tr::luxury_LuxuryForwardStatusForwarding(tr::now);
	} else {
		// ForwardState::State::Finished
		status = tr::luxury_LuxuryForwardStatusFinished(tr::now);
	}


	return std::make_pair(status, partString);
}

ForwardState::ForwardState(int totalChunks) {
	_data.totalChunks = totalChunks;
}

ForwardState::Snapshot ForwardState::snapshot() const {
	const auto lock = std::lock_guard(_mutex);
	return _data;
}

bool ForwardState::stopRequested() const {
	const auto lock = std::lock_guard(_mutex);
	return _data.stopRequested;
}

void ForwardState::requestStop() {
	const auto lock = std::lock_guard(_mutex);
	_data.stopRequested = true;
}

void ForwardState::setMessages(int total, int sent) {
	const auto lock = std::lock_guard(_mutex);
	_data.totalMessages = total;
	_data.sentMessages = sent;
}

void ForwardState::setSentMessages(int sent) {
	const auto lock = std::lock_guard(_mutex);
	_data.sentMessages = sent;
}

void ForwardState::advanceChunk() {
	const auto lock = std::lock_guard(_mutex);
	++_data.currentChunk;
}

void ForwardState::updateBottomBar(
		base::weak_ptr<Main::Session> session,
		PeerId peer,
		State state) {
	{
		const auto lock = std::lock_guard(_mutex);
		_data.state = state;
	}
	crl::on_main(session, [session, peer] {
		const auto strong = session.get();
		if (!strong) {
			return;
		}
		strong->changes().peerUpdated(
			strong->data().peer(peer),
			Data::PeerUpdate::Flag::Rights);
	});
}

Ui::PreparedList PrepareMedia(
		const std::vector<ForwardItem> &items,
		int &i,
		std::vector<const ForwardItem*> &groupItems) {
	const auto prepare = [&](const ForwardItem &item) {
		auto prepared = Ui::PreparedFile(item.path);
		if (prepared.path.isEmpty()) {
			// otherwise will fail assertion in PrepareDetails
			return prepared;
		}
		prepared.displayName = item.displayName;
		Storage::PrepareDetails(prepared, st::sendMediaPreviewSize, PhotoSideLimit());
		groupItems.push_back(&item);
		return prepared;
	};

	const auto &startItem = items[i];
	const auto groupId = startItem.groupId;

	Ui::PreparedList list;
	if (auto prepared = prepare(startItem); !prepared.path.isEmpty()) {
		list.files.emplace_back(std::move(prepared));
	}

	if (!groupId.value) {
		return list;
	}

	for (auto k = i + 1; k < int(items.size()); ++k) {
		const auto &nextItem = items[k];
		if (nextItem.groupId != groupId) {
			break;
		}
		if (auto prepared = prepare(nextItem); !prepared.path.isEmpty()) {
			list.files.emplace_back(std::move(prepared));
		}
		i = k;
	}
	return list;
}

void sendMedia(
	LuxurySync::WeakSession session,
	const std::shared_ptr<Ui::PreparedBundle> &bundle,
	const ForwardItem &primaryItem,
	Api::MessageToSend &&message,
	bool sendImagesAsPhotos,
	const LuxurySync::Cancelled &cancelled) {
	if (primaryItem.sticker) {
		LuxurySync::sendStickerSync(
			session,
			std::move(message),
			primaryItem.id,
			cancelled);
		return;
	}

	auto mediaType = [&] {
		if (primaryItem.document) {
			if (primaryItem.voice) {
				return SendMediaType::Audio;
			} else if (primaryItem.round) {
				return SendMediaType::Round;
			} else if (primaryItem.video) {
				// to send video as video need to pass it as 'photo'
				// ref: `void HistoryWidget::sendingFilesConfirmed`
				return SendMediaType::Photo;
			}
			return SendMediaType::File;
		}
		return SendMediaType::Photo;
	}();

	if (mediaType == SendMediaType::Round || mediaType == SendMediaType::Audio) {
		const auto path = bundle->groups.front().list.files.front().path;

		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			LOG(("failed to open file for forward with reason: %1").arg(file.errorString()));
		} else if (file.size() > 0 && file.size() <= kMaxVoiceBytes) {
			const auto data = file.read(kMaxVoiceBytes + 1);
			if (!data.isEmpty() && data.size() <= kMaxVoiceBytes) {
				LuxurySync::sendVoiceSync(
					session,
					data,
					primaryItem.duration,
					mediaType == SendMediaType::Round,
					std::move(message),
					cancelled);
				return;
			}
		}
		// Keep large round videos and voice messages off the heap.
		mediaType = (mediaType == SendMediaType::Round)
			? SendMediaType::Photo
			: SendMediaType::File;
	}

	// workaround for media albums consisting of video and photos
	if (sendImagesAsPhotos) {
		mediaType = SendMediaType::Photo;
	}

	for (auto &group : bundle->groups) {
		if (cancelled && cancelled()) {
			break;
		}
		LuxurySync::sendDocumentSync(
			session,
			group,
			mediaType,
			std::move(message.textWithTags),
			message.action,
			cancelled);
	}
}

bool isLuxuryForwardNeeded(const std::vector<not_null<HistoryItem*>> &items) {
	const auto needLuxuryForward = [&](const auto &item)
	{
		return isLuxuryForwardNeeded(item);
	};
	return std::ranges::any_of(items, needLuxuryForward);
}

bool isLuxuryForwardNeeded(not_null<HistoryItem*> item) {
	if (item->isDeleted() || item->isLuxuryNoForwards() || item->unsupportedTTL() || (item->media() && item->media()->ttlSeconds())) {
		return true;
	}
	return false;
}

bool isFullLuxuryForwardNeeded(not_null<HistoryItem*> item) {
	return item->from()->isLuxuryNoForwards() || item->history()->peer->isLuxuryNoForwards();
}

namespace {

std::vector<FullMsgId> ItemIds(const std::vector<ForwardItem> &items) {
	auto result = std::vector<FullMsgId>();
	result.reserve(items.size());
	for (const auto &item : items) {
		result.push_back(item.id);
	}
	return result;
}

void LoadDocuments(
		const ForwardJob &job,
		const std::vector<ForwardItem> &items,
		const LuxurySync::Cancelled &cancelled) {
	for (const auto &item : items) {
		if (cancelled && cancelled()) {
			return;
		} else if (!item.downloadable) {
			continue;
		}
		const auto file = QFile(item.path);
		const auto size = file.exists() ? file.size() : 0;
		if (size == item.expectedSize) {
			continue;
		} else if (item.document) {
			LuxurySync::loadDocumentSync(
				job.session,
				item.id,
				item.path,
				item.expectedSize,
				cancelled);
		} else if (item.photo) {
			LuxurySync::loadPhotoSync(
				job.session,
				item.id,
				item.path,
				cancelled);
		}
	}
}

void ForwardItems(
		const ForwardJob &job,
		const std::shared_ptr<ForwardState> &state,
		const std::vector<ForwardItem> &items) {
	const auto cancelled = [state, session = job.session] {
		return state->stopRequested() || !session.get();
	};
	state->setMessages(int(items.size()), 0);
	if (std::ranges::any_of(items, &ForwardItem::downloadable)) {
		state->updateBottomBar(
			job.session,
			job.peerId,
			ForwardState::State::Downloading);
		LoadDocuments(job, items, cancelled);
	}
	if (cancelled()) {
		return;
	}

	state->updateBottomBar(
		job.session,
		job.peerId,
		ForwardState::State::Sending);
	for (auto i = 0; i != int(items.size()); ++i) {
		const auto &item = items[i];
		if (cancelled()) {
			return;
		} else if (item.text.empty() && !item.downloadable) {
			continue;
		}

		auto message = Api::MessageToSend(job.action);
		message.action.options.invertCaption = item.invertCaption;
		if (job.options != Data::ForwardOptions::NoNamesAndCaptions) {
			message.textWithTags = item.text;
		}

		if (!item.downloadable) {
			LuxurySync::sendMessageSync(
				job.session,
				std::move(message),
				cancelled);
		} else {
			auto groupItems = std::vector<const ForwardItem*>();
			auto preparedMedia = PrepareMedia(items, i, groupItems);
			for (auto j = int(preparedMedia.files.size()); j > 0;) {
				--j;
				const auto groupItem = groupItems[j];
				const auto file = QFile(preparedMedia.files[j].path);
				const auto size = file.exists() ? file.size() : 0;
				if ((groupItem->photo || groupItem->document)
					&& size < groupItem->expectedSize) {
					preparedMedia.files.erase(preparedMedia.files.begin() + j);
					groupItems.erase(groupItems.begin() + j);
				}
			}
			if (preparedMedia.files.empty()) {
				continue;
			}

			auto way = Ui::SendFilesWay();
			way.setGroupFiles(true);
			way.setSendImagesAsPhotos(
				std::ranges::any_of(groupItems, [](const auto item) {
					return item->photo;
				}));
			auto groups = Ui::DivideByGroups(
				std::move(preparedMedia),
				way,
				job.slowmodeApplied);
			auto bundle = Ui::PrepareFilesBundle(
				std::move(groups),
				way,
				false);
			sendMedia(
				job.session,
				bundle,
				*groupItems.front(),
				std::move(message),
				way.sendImagesAsPhotos(),
				cancelled);
		}
		if (cancelled()) {
			return;
		}
		state->setSentMessages(i + 1);
		state->updateBottomBar(
			job.session,
			job.peerId,
			ForwardState::State::Sending);
	}
}

void RunForward(
		const ForwardJob &job,
		const std::vector<ForwardItem> &items,
		const std::shared_ptr<ForwardState> &state) {
	ForwardItems(job, state, items);
	FinishForward(
		job.peerId,
		state,
		job.session,
		job.sessionKey);
}

void RunIntelligentForward(
		const ForwardJob &job,
		const std::vector<ForwardChunk> &chunks,
		const std::shared_ptr<ForwardState> &state) {
	const auto cancelled = [state, session = job.session] {
		return state->stopRequested() || !session.get();
	};
	for (const auto &chunk : chunks) {
		if (cancelled()) {
			break;
		} else if (chunk.luxury) {
			ForwardItems(job, state, chunk.items);
		} else {
			state->setMessages(int(chunk.items.size()), 0);
			state->updateBottomBar(
				job.session,
				job.peerId,
				ForwardState::State::Sending);
			LuxurySync::forwardMessagesSync(
				job.session,
				ItemIds(chunk.items),
				job.action,
				job.options,
				cancelled);
			if (!cancelled()) {
				state->setSentMessages(int(chunk.items.size()));
			}
		}
		if (cancelled()) {
			break;
		}
		state->advanceChunk();
	}
	FinishForward(
		job.peerId,
		state,
		job.session,
		job.sessionKey);
}

void ClearForwardDraft(const Api::SendAction &action) {
	action.history->setForwardDraft(
		action.replyTo.topicRootId,
		action.replyTo.monoforumPeerId,
		{});
}

} // namespace

void intelligentForward(
		not_null<Main::Session*> session,
		const Api::SendAction &action,
		const Data::ResolvedForwardDraft &draft) {
	ClearForwardDraft(action);
	if (draft.items.empty()) {
		return;
	}
	auto chunks = std::vector<ForwardChunk>();
	for (auto &item : SnapshotItems(session, draft.items)) {
		if (chunks.empty() || chunks.back().luxury != item.luxury) {
			chunks.push_back(ForwardChunk{ .luxury = item.luxury });
		}
		chunks.back().items.push_back(std::move(item));
	}
	auto job = SnapshotJob(session, action, draft.options);
	auto state = std::make_shared<ForwardState>(int(chunks.size()));
	SetForwardState(job.peerId, job.sessionKey, state);
	crl::async([
		job = std::move(job),
		chunks = std::move(chunks),
		state
	] {
		RunIntelligentForward(job, chunks, state);
	});
}

void forwardMessages(
		not_null<Main::Session*> session,
		const Api::SendAction &action,
		const Data::ResolvedForwardDraft &draft) {
	ClearForwardDraft(action);
	if (draft.items.empty()) {
		return;
	}
	auto job = SnapshotJob(session, action, draft.options);
	auto items = SnapshotItems(session, draft.items);
	auto state = std::make_shared<ForwardState>(1);
	SetForwardState(job.peerId, job.sessionKey, state);
	crl::async([
		job = std::move(job),
		items = std::move(items),
		state
	] {
		RunForward(job, items, state);
	});
}

} // namespace LuxuryForward
