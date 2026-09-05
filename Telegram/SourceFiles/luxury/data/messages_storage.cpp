// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/data/messages_storage.h"

#include "luxury/data/luxury_database.h"
#include "luxury/features/watch/watched_media.h"
#include "luxury/luxury_settings.h"
#include "luxury/utils/luxury_mapper.h"
#include "luxury/utils/telegram_helpers.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"

namespace {

// Keyed by the account, like every message row: peer ids are global in
// Telegram, so one table covers every account. Sits outside LuxuryMessages
// so LuxuryOnline shares it -- it used to live in there, and the online
// recorder could not see it.
ID DatabaseUserId(const Main::Session &session) {
	return static_cast<ID>(session.uniqueId());
}

} // namespace

namespace LuxuryMessages {

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
	// isForum(), not topic(): HistoryItem::topic() is null until a ForumTopic
	// object exists, so in a forum the user has not browsed this run the row used
	// to be written with topicId 0. The viewer passes the open topic's root id,
	// and the query's "or topicId == 0" disjunct is evaluated against the
	// argument, not the column, so a 0 row never matched and the message read as
	// never saved. topicRootId() falls back to kGeneralId, so it must stay behind
	// the isForum() test -- an ordinary chat has no topic id at all.
	message.topicId = item->history()->peer->isForum()
		? item->topicRootId().bare
		: ID();
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

} // namespace LuxuryMessages

namespace LuxuryOnline {

void recordTransition(not_null<UserData*> user, bool online, int at) {
	const not_null<PeerData*> peer = user;
	const auto userId = DatabaseUserId(peer->session());
	const auto dialogId = getDialogIdFromPeer(peer);
	const auto peerId = static_cast<ID>(peer->id.value);
	// Resolve everything main-thread-only here; the row itself goes through
	// the ordered queue so rapid online/offline flaps keep their order.
	LuxuryDatabase::async([=] {
		auto event = OnlineEvent();
		event.userId = userId;
		event.dialogId = dialogId;
		event.peerId = peerId;
		event.online = online;
		event.at = at;
		LuxuryDatabase::addOnlineEvent(std::move(event));
	});
}

std::vector<OnlineEvent> getHistory(not_null<PeerData*> peer, int totalLimit) {
	return LuxuryDatabase::getOnlineEvents(
		DatabaseUserId(peer->session()),
		getDialogIdFromPeer(peer),
		totalLimit);
}

// Single gate for the server-driven presence hook in Session::processUser.
// Bots and service accounts never transition for real; a transition the
// update already applied is compared against the pre-update state the caller
// captured, so only genuine flaps reach the disk. Presence slices keep
// arriving under a passcode lock -- only UI is gated there -- so recording
// while locked is the default, and the toggle opts out of it.
void noteServerLastseen(not_null<UserData*> user, bool wasOnline, int now) {
	const auto &settings = LuxurySettings::getInstance();
	if (!settings.trackOnlineHistory()) {
		return;
	}
	if (user->isBot() || user->isServiceUser()) {
		return;
	}
	if (Core::App().passcodeLocked()
		&& !settings.trackOnlineEvenWhenLocked()) {
		return;
	}
	if (wasOnline == user->lastseen().isOnline(now)) {
		return;
	}
	recordTransition(user, user->lastseen().isOnline(now), now);
}

void clearHistory(not_null<PeerData*> peer) {
	const auto userId = DatabaseUserId(peer->session());
	const auto dialogId = getDialogIdFromPeer(peer);
	LuxuryDatabase::async([=] {
		LuxuryDatabase::clearOnlineEvents(userId, dialogId);
	});
}

} // namespace LuxuryOnline
