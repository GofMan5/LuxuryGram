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

class DeletedMessage : public LuxuryMessageBase
{
};

class EditedMessage : public LuxuryMessageBase
{
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
