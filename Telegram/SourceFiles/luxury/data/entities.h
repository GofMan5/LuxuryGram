// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <string>

using ID = long long;

class LuxuryMessageBase
{
public:
	ID fakeId = 0;
	ID userId = 0;
	ID dialogId = 0;
	ID groupedId = 0;
	ID peerId = 0;
	ID fromId = 0;
	ID topicId = 0;
	ID messageId = 0;
	int date = 0;
	int flags = 0;
	int editDate = 0;
	int views = 0;
	std::string postAuthor;
	int entityCreateDate = 0;
	std::string text;
	std::vector<char> textEntities;
};

// Every field below is dead in the sense that nothing reads or writes it -- the
// media-preserving feature they were scaffolding for was never implemented, here
// or upstream, and mediaPath is the literal "/" in all 31.8M rows of a real
// database. They are declared anyway because existing DeletedMessage tables have
// the columns, and a column the storage does not declare is one sqlite_orm drops
// with an ALTER TABLE DROP COLUMN -- which SQLite implements by rewriting every
// row. Dropping these fourteen took a 12 GB table through fourteen rewrites and
// hung the client at startup for as long as it was left running.
//
// EditedMessage deliberately does not get them: that table is already down to
// sixteen columns in the wild, so declaring them there would create the very
// mismatch this avoids.
class DeletedMessage : public LuxuryMessageBase
{
public:
	std::string fwdPostAuthor;
	int replyFlags = 0;
	int replyMessageId = 0;
	ID replyPeerId = 0;
	int replyTopId = 0;
	bool replyForumTopic = false;
	std::vector<char> replySerialized;
	std::string mediaPath;
	std::string hqThumbPath;
	int documentType = 0;
	std::vector<char> documentSerialized;
	std::vector<char> thumbsSerialized;
	std::vector<char> documentAttributesSerialized;
	std::string mimeType;
};

class EditedMessage : public LuxuryMessageBase
{
};

// One row per online/offline transition of a user, written from the
// Session::processUser hook while trackOnlineHistory is on. dialogId reuses getDialogIdFromPeer, the
// same key the message tables use; online is 1 for "came online", 0 for "went
// offline"; at is a unixtime. No index beyond the primary key: reads are always
// "latest N for one peer".
class OnlineEvent
{
public:
	ID fakeId = 0;
	ID userId = 0;
	ID dialogId = 0;
	ID peerId = 0;
	bool online = false;
	int at = 0;
};

class RegexFilter
{
public:
	std::vector<char> id;
	std::string text;
	bool enabled = false;
	bool reversed = false;
	bool caseInsensitive = false;
	std::optional<ID> dialogId; // nullable

	bool operator==(const RegexFilter &other) const {
		return id == other.id &&
			text == other.text &&
			caseInsensitive == other.caseInsensitive &&
			reversed == other.reversed &&
			dialogId == other.dialogId &&
			enabled == other.enabled;
	}
};

class RegexFilterGlobalExclusion
{
public:
	ID fakeId = 0;
	ID dialogId = 0;
	std::vector<char> filterId;

	bool operator==(const RegexFilterGlobalExclusion& other) const {
		return dialogId == other.dialogId && filterId == other.filterId;
	}
};
