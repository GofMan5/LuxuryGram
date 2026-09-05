// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "luxury/data/entities.h"

#include <functional>

class SchemaVersion
{
public:
	int id = 0;
	int version = 0;
};

namespace LuxuryDatabase {

void initialize();

// Runs work on the one thread the database is allowed to block. crl::async would
// do the work off the main thread too, but out of order: a clear posted before an
// insert could land after it and wipe the row the insert just added.
void async(FnMut<void()> &&work);

// Blocks until everything already posted has run. Called once, from application
// teardown: nothing else joins the queue, so without it the rows posted by the
// last delete before a quit are dropped, and a pass can still be inside an insert
// while the globals it writes through are being destroyed.
void shutdown();

// ponytail: hasRevisions, hasFilters and hasPerDialogFilters run on the main
// thread. Each gates a menu row or a settings block that is built in one
// synchronous pass, so there is nothing to await into -- and each is a single
// indexed `SELECT ... LIMIT 1`, with the connection held open and every
// long-running operation moved off the main thread, so the query is all they
// cost. If one ever shows up in a profile, the way out is a placeholder row that
// fills in later, the way Ui::WhoReactedContextAction does.

void addEditedMessage(const EditedMessage &message);
std::vector<EditedMessage> getEditedMessages(ID userId, ID dialogId, ID messageId, ID minId, ID maxId, int totalLimit);
bool hasRevisions(ID userId, ID dialogId, ID messageId);

void addDeletedMessage(DeletedMessage message);
void addDeletedMessages(std::vector<DeletedMessage> &&messages);
std::vector<DeletedMessage> getDeletedMessages(ID userId, ID dialogId, ID topicId, ID minId, ID maxId, int totalLimit, const std::string &searchQuery = "");
void removeDeletedMessage(ID userId, ID dialogId, ID messageId);
void clearDeletedMessages(ID userId, ID dialogId, ID topicId);

void addOnlineEvent(OnlineEvent event);
std::vector<OnlineEvent> getOnlineEvents(ID userId, ID dialogId, int totalLimit);
void clearOnlineEvents(ID userId, ID dialogId);

std::vector<RegexFilter> getAllRegexFilters();
std::vector<RegexFilter> getShared();
std::vector<RegexFilter> getByDialogId(ID dialogId);
std::vector<RegexFilterGlobalExclusion> getAllFiltersExclusions();
std::vector<RegexFilter> getExcludedByDialogId(ID dialogId);

bool applyFilterChanges(
	const std::vector<RegexFilter> &newFilters,
	const std::vector<std::vector<char>> &removeFiltersById,
	const std::vector<RegexFilter> &filterOverrides,
	const std::vector<RegexFilterGlobalExclusion> &newExclusions,
	const std::vector<RegexFilterGlobalExclusion> &removeExclusions);

// Every write below reports whether it reached the disk: a filter that failed
// to save must not be shown as saved.
bool addRegexFilter(const RegexFilter &filter);
bool addRegexExclusion(const RegexFilterGlobalExclusion &exclusion);

bool updateRegexFilter(const RegexFilter &filter);

bool deleteFilter(const std::vector<char> &id);
bool deleteExclusionsByFilterId(const std::vector<char> &id);
bool deleteExclusion(ID dialogId, const std::vector<char> &filterId);

bool deleteAllFilters();
bool deleteAllExclusions();

bool hasFilters();
bool hasPerDialogFilters();

[[nodiscard]] bool moveCurrentDatabase();

}
