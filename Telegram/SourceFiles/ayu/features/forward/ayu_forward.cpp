// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/forward/ayu_forward.h"

#include "apiwrap.h"
#include "lang_auto.h"
#include "ayu/features/forward/ayu_sync.h"
#include "ayu/utils/telegram_helpers.h"
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

namespace AyuForward {

namespace {

std::mutex ForwardStatesMutex;
std::unordered_map<PeerId, std::shared_ptr<ForwardState>> ForwardStates;
constexpr auto kMaxVoiceBytes = 64 * 1024 * 1024;

std::shared_ptr<ForwardState> FindForwardState(PeerId id) {
	const auto lock = std::lock_guard(ForwardStatesMutex);
	const auto i = ForwardStates.find(id);
	return (i != end(ForwardStates)) ? i->second : nullptr;
}

void SetForwardState(PeerId id, std::shared_ptr<ForwardState> state) {
	const auto lock = std::lock_guard(ForwardStatesMutex);
	ForwardStates[id] = std::move(state);
}

void RemoveForwardState(
		PeerId id,
		const std::shared_ptr<ForwardState> &state) {
	const auto lock = std::lock_guard(ForwardStatesMutex);
	const auto i = ForwardStates.find(id);
	if (i != end(ForwardStates) && i->second == state) {
		ForwardStates.erase(i);
	}
}

void FinishForward(
		PeerId id,
		const std::shared_ptr<ForwardState> &state,
		const Main::Session &session) {
	state->updateBottomBar(session, id, ForwardState::State::Finished);
	RemoveForwardState(id, state);
}

} // namespace

bool isForwarding(const PeerId &id) {
	const auto state = id.value ? FindForwardState(id) : nullptr;
	if (!state) {
		return false;
	}
	const auto snapshot = state->snapshot();
	return snapshot.state != ForwardState::State::Finished
		&& snapshot.currentChunk < snapshot.totalChunks
		&& !snapshot.stopRequested;
}

void cancelForward(const PeerId &id, const Main::Session &session) {
	if (const auto state = FindForwardState(id)) {
		state->requestStop();
		FinishForward(id, state, session);
	}
}

std::pair<QString, QString> stateName(const PeerId &id) {
	const auto state = FindForwardState(id);
	if (!state) {
		return std::make_pair(QString(), QString());
	}
	const auto snapshot = state->snapshot();

	QString messagesString = tr::ayu_AyuForwardStatusSentCount(tr::now,
														   lt_count1,
														   QString::number(snapshot.sentMessages),
														   lt_count2,
														   QString::number(snapshot.totalMessages)

	);

	QString chunkString = tr::ayu_AyuForwardStatusChunkCount(tr::now,
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
		status = tr::ayu_AyuForwardStatusPreparing(tr::now);
	} else if (snapshot.state == ForwardState::State::Downloading) {
		return std::make_pair(tr::ayu_AyuForwardStatusLoadingMedia(tr::now), "");
	} else if (snapshot.state == ForwardState::State::Sending) {
		status = tr::ayu_AyuForwardStatusForwarding(tr::now);
	} else {
		// ForwardState::State::Finished
		status = tr::ayu_AyuForwardStatusFinished(tr::now);
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
		const Main::Session &session,
		PeerId peer,
		State state) {
	{
		const auto lock = std::lock_guard(_mutex);
		_data.state = state;
	}
	const auto sessionPtr = &session;
	crl::on_main(sessionPtr, [sessionPtr, peer] {
		sessionPtr->changes().peerUpdated(
			sessionPtr->data().peer(peer),
			Data::PeerUpdate::Flag::Rights);
	});
}

static Ui::PreparedList prepareMedia(not_null<Main::Session*> session,
									 const std::vector<not_null<HistoryItem*>> &items,
									 int &i,
									 std::vector<not_null<Data::Media*>> &groupMedia) {
	const auto prepare = [&](not_null<Data::Media*> media)
	{
		auto prepared = Ui::PreparedFile(AyuSync::filePath(session, media));
		if (prepared.path.isEmpty()) {
			// otherwise will fail assertion in PrepareDetails
			return prepared;
		}
		Storage::PrepareDetails(prepared, st::sendMediaPreviewSize, PhotoSideLimit());
		groupMedia.emplace_back(media);
		return prepared;
	};

	const auto startItem = items[i];
	const auto media = startItem->media();
	const auto groupId = startItem->groupId();

	Ui::PreparedList list;
	if (auto prepared = prepare(media); !prepared.path.isEmpty()) {
		list.files.emplace_back(std::move(prepared));
	}

	if (!groupId.value) {
		return list;
	}

	for (int k = i + 1; k < items.size(); ++k) {
		const auto nextItem = items[k];
		if (nextItem->groupId() != groupId) {
			break;
		}
		if (const auto nextMedia = nextItem->media()) {
			if (auto prepared = prepare(nextMedia); !prepared.path.isEmpty()) {
				list.files.emplace_back(std::move(prepared));
			}
			i = k;
		}
	}
	return list;
}

void sendMedia(
	not_null<Main::Session*> session,
	const std::shared_ptr<Ui::PreparedBundle> &bundle,
	not_null<Data::Media*> primaryMedia,
	Api::MessageToSend &&message,
	bool sendImagesAsPhotos) {
	if (const auto document = primaryMedia->document(); document && document->sticker()) {
		AyuSync::sendStickerSync(session, std::move(message), document);
		return;
	}

	auto mediaType = [&]
	{
		if (const auto document = primaryMedia->document()) {
			if (document->isVoiceMessage()) {
				return SendMediaType::Audio;
			} else if (document->isVideoMessage()) {
				return SendMediaType::Round;
			} else if (document->isVideoFile() || document->isGifv()) {
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
				AyuSync::sendVoiceSync(
					session,
					data,
					primaryMedia->document()->duration(),
					mediaType == SendMediaType::Round,
					std::move(message));
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
		AyuSync::sendDocumentSync(
			session,
			group,
			mediaType,
			std::move(message.textWithTags),
			message.action);
	}
}

bool isAyuForwardNeeded(const std::vector<not_null<HistoryItem*>> &items) {
	const auto needAyuForward = [&](const auto &item)
	{
		return isAyuForwardNeeded(item);
	};
	return std::ranges::any_of(items, needAyuForward);
}

bool isAyuForwardNeeded(not_null<HistoryItem*> item) {
	if (item->isDeleted() || item->isAyuNoForwards() || item->unsupportedTTL() || (item->media() && item->media()->ttlSeconds())) {
		return true;
	}
	return false;
}

bool isFullAyuForwardNeeded(not_null<HistoryItem*> item) {
	return item->from()->isAyuNoForwards() || item->history()->peer->isAyuNoForwards();
}

struct ForwardChunk
{
	bool isAyuForwardNeeded;
	std::vector<not_null<HistoryItem*>> items;
};

void intelligentForward(
	not_null<Main::Session*> session,
	const Api::SendAction &action,
	const Data::ResolvedForwardDraft &draft) {
	const auto history = action.history;
	const auto topicRootId = action.replyTo.topicRootId;
	const auto monoforumPeerId = action.replyTo.monoforumPeerId;
	crl::on_main([=]
	{
		history->setForwardDraft(topicRootId, monoforumPeerId, {});
	});

	const auto items = draft.items;
	if (items.empty()) {
		return;
	}
	const auto peer = history->peer;

	auto chunks = std::vector<ForwardChunk>();
	auto currentArray = std::vector<not_null<HistoryItem*>>();

	auto currentChunk = ForwardChunk({
		.isAyuForwardNeeded = isAyuForwardNeeded(items[0]),
		.items = currentArray
	});

	for (const auto &item : items) {
		if (isAyuForwardNeeded(item) != currentChunk.isAyuForwardNeeded) {
			currentChunk.items = currentArray;
			chunks.push_back(currentChunk);

			currentArray = std::vector<not_null<HistoryItem*>>();

			currentChunk = ForwardChunk({
				.isAyuForwardNeeded = isAyuForwardNeeded(item),
				.items = currentArray
			});
		}
		currentArray.push_back(item);
	}

	currentChunk.items = currentArray;
	chunks.push_back(currentChunk);

	auto state = std::make_shared<ForwardState>(chunks.size());
	SetForwardState(peer->id, state);


	for (const auto &chunk : chunks) {
		if (state->stopRequested()) {
			break;
		}
		if (chunk.isAyuForwardNeeded) {
			forwardMessages(session, action, true, Data::ResolvedForwardDraft(chunk.items));
		} else {
			state->setMessages(chunk.items.size(), 0);
			state->updateBottomBar(*session, peer->id, ForwardState::State::Sending);

			AyuSync::forwardMessagesSync(session, chunk.items, action, draft.options);

			state->setSentMessages(chunk.items.size());
		}
		state->advanceChunk();
	}

	FinishForward(peer->id, state, *session);
}

void forwardMessages(
	not_null<Main::Session*> session,
	const Api::SendAction &action,
	bool reuseState,
	const Data::ResolvedForwardDraft &draft) {
	const auto items = draft.items;
	const auto history = action.history;
	const auto peer = history->peer;

	const auto topicRootId = action.replyTo.topicRootId;
	const auto monoforumPeerId = action.replyTo.monoforumPeerId;
	crl::on_main([=]
	{
		history->setForwardDraft(topicRootId, monoforumPeerId, {});
	});

	auto state = reuseState
		? FindForwardState(peer->id)
		: std::make_shared<ForwardState>(1);
	if (!state) {
		return;
	}
	if (!reuseState) {
		SetForwardState(peer->id, state);
	}

	std::vector<not_null<HistoryItem*>> toBeDownloaded;


	for (const auto item : items) {
		if (mediaDownloadable(item->media())) {
			toBeDownloaded.push_back(item);
		}
	}
	state->setMessages(items.size(), 0);
	if (!toBeDownloaded.empty()) {
		state->updateBottomBar(*session, peer->id, ForwardState::State::Downloading);
		AyuSync::loadDocuments(session, toBeDownloaded);
	}


	state->updateBottomBar(*session, peer->id, ForwardState::State::Sending);

	for (int i = 0; i < items.size(); i++) {
		const auto item = items[i];

		if (state->stopRequested()) {
			if (!reuseState) {
				FinishForward(peer->id, state, *session);
			}
			return;
		}

		auto extractedText = extractText(item);
		if (extractedText.empty() && !mediaDownloadable(item->media())) {
			continue;
		}

		auto message = Api::MessageToSend(Api::SendAction(session->data().history(peer->id)));
		message.action.options.invertCaption = item->invertMedia();
		message.action.replyTo = action.replyTo;

		if (draft.options != Data::ForwardOptions::NoNamesAndCaptions) {
			message.textWithTags = extractedText;
		}

		if (!mediaDownloadable(item->media())) {
			AyuSync::sendMessageSync(session, std::move(message));
		} else if (const auto media = item->media()) {
			if (media->poll()) {
				AyuSync::sendMessageSync(session, std::move(message));
				continue;
			}

			std::vector<not_null<Data::Media*>> groupMedia;
			auto preparedMedia = prepareMedia(session, items, i, groupMedia);

			// remove not finished files
			for (int j = preparedMedia.files.size() - 1; j >= 0; j--) {
				auto &file = preparedMedia.files[j];

				QFile f(file.path);
				if (
                    (groupMedia[j]->photo() && f.size() < groupMedia[j]->photo()->imageByteSize(Data::PhotoSize::Large)) ||
					(groupMedia[j]->document() && f.size() < groupMedia[j]->document()->size)
				) {
					preparedMedia.files.erase(preparedMedia.files.begin() + j);
					groupMedia.erase(groupMedia.begin() + j);
				}
			}

			if (preparedMedia.files.empty()) {
				continue;
			}
			Ui::SendFilesWay way;
			way.setGroupFiles(true);
			way.setSendImagesAsPhotos(false);
			for (const auto &media : groupMedia) {
				if (media->photo()) {
					way.setSendImagesAsPhotos(true);
					break;
				}
			}

			auto groups = Ui::DivideByGroups(
				std::move(preparedMedia),
				way,
				peer->slowmodeApplied());

			auto bundle = Ui::PrepareFilesBundle(
				std::move(groups),
				way,
				false);
			sendMedia(
				session,
				bundle,
				groupMedia.front(),
				std::move(message),
				way.sendImagesAsPhotos());
		}
		// if there are grouped messages
		// "i" is incremented in prepareMedia

		state->setSentMessages(i + 1);
		state->updateBottomBar(*session, peer->id, ForwardState::State::Sending);
	}
	if (!reuseState) {
		FinishForward(peer->id, state, *session);
	}
}

} // namespace AyuFeatures::AyuForward
