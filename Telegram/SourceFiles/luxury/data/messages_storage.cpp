// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/data/messages_storage.h"

#include "luxury/data/luxury_database.h"
#include "luxury/features/watch/watched_media.h"
#include "luxury/utils/luxury_mapper.h"
#include "luxury/utils/telegram_helpers.h"
#include "base/unixtime.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"

namespace LuxuryMessages {

namespace {

ID DatabaseUserId(const Main::Session &session) {
	return static_cast<ID>(session.uniqueId());
}

} // namespace

template<typename DerivedMessage>
std::vector<LuxuryMessageBase> convertToBase(std::vector<DerivedMessage> messages) {
	std::vector<LuxuryMessageBase> based;
	based.reserve(messages.size());
	for (auto &message : messages) {
		based.push_back(std::move(static_cast<LuxuryMessageBase&>(message)));
	}
	return based;
}

void map(not_null<HistoryItem*> item, LuxuryMessageBase &message) {
	message.userId = DatabaseUserId(item->history()->session());
	message.dialogId = getDialogIdFromPeer(item->history()->peer);
	message.groupedId = item->groupId().raw();
	message.peerId = static_cast<ID>(item->history()->peer->id.value);
	message.fromId = static_cast<ID>(item->from()->id.value);
	message.topicId = item->topic() ? item->topicRootId().bare : ID();
	message.messageId = item->id.bare;
	message.date = item->date();
	message.flags = LuxuryMapper::mapItemFlagsToMTPFlags(item);

	if (const auto edited = item->Get<HistoryMessageEdited>()) {
		message.editDate = edited->date;
	} else {
		message.editDate = base::unixtime::now();
	}

	message.views = item->viewsCount();

	if (const auto msgsigned = item->Get<HistoryMessageSigned>()) {
		message.postAuthor = msgsigned->author.toStdString();
	}

	message.entityCreateDate = base::unixtime::now();

	auto serializedText = LuxuryMapper::serializeTextWithEntities(item);
	message.text = serializedText.first;
	message.textEntities = serializedText.second;
}

void addEditedMessage(not_null<HistoryItem *> item) {
	EditedMessage message;
	map(item, message);

	// map() goes through serializeTextWithEntities(), which falls back to the
	// media label ("Photo", "Voice message"), so an empty text here means there
	// is nothing left to show: the viewer only renders text messages.
	if (message.text.empty()) {
		return;
	}

	// Left synchronous on purpose: hasRevisions() is a synchronous main-thread
	// probe (see the note in luxury_database.h), so posting this would let a
	// right-click land before the revision it should be offering. One row, no
	// fsync under WAL.
	LuxuryDatabase::addEditedMessage(message);
}

std::vector<LuxuryMessageBase> getEditedMessages(not_null<HistoryItem*> item, ID minId, ID maxId, int totalLimit) {
	const auto userId = DatabaseUserId(item->history()->session());
	const auto dialogId = getDialogIdFromPeer(item->history()->peer);
	const auto msgId = item->id.bare;

	return getEditedMessages(userId, dialogId, msgId, minId, maxId, totalLimit);
}

std::vector<LuxuryMessageBase> getEditedMessages(
		ID userId,
		ID dialogId,
		ID messageId,
		ID minId,
		ID maxId,
		int totalLimit) {
	return convertToBase(LuxuryDatabase::getEditedMessages(
		userId,
		dialogId,
		messageId,
		minId,
		maxId,
		totalLimit));
}

bool hasRevisions(not_null<HistoryItem*> item) {
	const auto userId = DatabaseUserId(item->history()->session());
	const auto dialogId = getDialogIdFromPeer(item->history()->peer);
	const auto msgId = item->id.bare;

	return LuxuryDatabase::hasRevisions(userId, dialogId, msgId);
}

void addDeletedMessage(not_null<HistoryItem*> item) {
	DeletedMessage message;
	map(item, message);

	if (message.text.empty()) {
		return;
	}

	// After the check, not before: a row that is not written has nothing pointing
	// at the file, so keeping it would only leave an orphan behind. Left in the
	// prunable area instead.
	message.mediaPath =
		LuxuryFeatures::Watch::keepMediaForDeleted(item).toStdString();

	LuxuryDatabase::async([message = std::move(message)]() mutable {
		LuxuryDatabase::addDeletedMessage(std::move(message));
	});
}

void addDeletedMessages(const std::vector<not_null<HistoryItem*>> &items) {
	auto messages = std::vector<DeletedMessage>();
	messages.reserve(items.size());
	for (const auto &item : items) {
		auto message = DeletedMessage();
		map(item, message);
		if (!message.text.empty()) {
			message.mediaPath =
				LuxuryFeatures::Watch::keepMediaForDeleted(item).toStdString();
			messages.push_back(std::move(message));
		}
	}
	// A "delete all my messages" run posts these by the hundred, one transaction
	// each. Mapping needs the items and stays here; the write does not.
	LuxuryDatabase::async([messages = std::move(messages)]() mutable {
		LuxuryDatabase::addDeletedMessages(std::move(messages));
	});
}

std::vector<LuxuryMessageBase>
getDeletedMessages(not_null<PeerData*> peer, ID topicId, ID minId, ID maxId, int totalLimit, const QString &searchQuery) {
	const auto userId = DatabaseUserId(peer->session());
	return getDeletedMessages(
		userId,
		getDialogIdFromPeer(peer),
		topicId,
		minId,
		maxId,
		totalLimit,
		searchQuery);
}

std::vector<LuxuryMessageBase> getDeletedMessages(
		ID userId,
		ID dialogId,
		ID topicId,
		ID minId,
		ID maxId,
		int totalLimit,
		const QString &searchQuery) {
	return convertToBase(LuxuryDatabase::getDeletedMessages(
		userId,
		dialogId,
		topicId,
		minId,
		maxId,
		totalLimit,
		searchQuery.toStdString()));
}

void removeDeletedMessage(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	const auto userId = DatabaseUserId(peer->session());
	const auto dialogId = getDialogIdFromPeer(peer);
	const auto messageId = item->id.bare;
	LuxuryDatabase::async([=] {
		LuxuryDatabase::removeDeletedMessage(userId, dialogId, messageId);
	});
}

void clearDeletedMessages(not_null<PeerData*> peer, ID topicId) {
	const auto userId = DatabaseUserId(peer->session());
	const auto dialogId = getDialogIdFromPeer(peer);
	// Nothing waits for it: the caller has already dropped the items it was
	// showing. Resolve the ids here, though -- PeerData is main-thread only.
	LuxuryDatabase::async([=] {
		LuxuryDatabase::clearDeletedMessages(userId, dialogId, topicId);
	});
}

}
