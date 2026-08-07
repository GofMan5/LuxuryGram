// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "luxury/data/entities.h"

namespace LuxuryMessages {

void addEditedMessage(not_null<HistoryItem *> item);
std::vector<LuxuryMessageBase> getEditedMessages(not_null<HistoryItem*> item, ID minId, ID maxId, int totalLimit);
std::vector<LuxuryMessageBase> getEditedMessages(
	ID userId,
	ID dialogId,
	ID messageId,
	ID minId,
	ID maxId,
	int totalLimit);
bool hasRevisions(not_null<HistoryItem*> item);

void addDeletedMessage(not_null<HistoryItem*> item);
std::vector<LuxuryMessageBase> getDeletedMessages(not_null<PeerData*> peer, ID topicId, ID minId, ID maxId, int totalLimit, const QString &searchQuery = QString());
std::vector<LuxuryMessageBase> getDeletedMessages(
	ID userId,
	ID dialogId,
	ID topicId,
	ID minId,
	ID maxId,
	int totalLimit,
	const QString &searchQuery = QString());
bool hasDeletedMessages(not_null<PeerData*> peer, ID topicId);
void removeDeletedMessage(not_null<HistoryItem*> item);
void clearDeletedMessages(not_null<PeerData*> peer, ID topicId);

}
