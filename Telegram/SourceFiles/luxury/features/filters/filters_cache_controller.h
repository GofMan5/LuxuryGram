// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "luxury/data/entities.h"
#include "luxury/features/filters/filters_controller.h"
#include "rpl/producer.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace Data {
struct Group;
}

namespace FiltersCacheController {

inline constexpr auto kMaxPatternLength = 16 * 1024;

using HashablePattern = FiltersController::HashablePattern;
using PatternHasher = FiltersController::PatternHasher;
using ReversiblePattern = FiltersController::ReversiblePattern;

void fireUpdate();
[[nodiscard]] rpl::producer<> updates();

// Re-reads every filter from the database and recompiles every pattern. Reads
// the whole filter table and runs the ICU compiler, so it must not be called on
// the main thread: post it through LuxuryDatabase::async, which also keeps it
// ordered behind the write it is meant to pick up.
void reloadNow();

// Throws away the per-message results without touching the patterns. Enough for
// anything that changes what counts as hidden but not the patterns themselves,
// like the shadow ban list or the master toggle.
void dropResults();

struct Cache
{
	std::vector<HashablePattern> sharedPatterns;
	std::unordered_map<ID, std::vector<ReversiblePattern>> patternsByDialogId;
	std::unordered_map<ID, std::unordered_set<HashablePattern, PatternHasher>> exclusionsByDialogId;
};

[[nodiscard]] std::shared_ptr<const Cache> snapshot();

std::optional<bool> isFiltered(not_null<HistoryItem*> item);
bool hasFilteredMessages(not_null<PeerData*> peer);
void putHiddenBlockedMessage(not_null<HistoryItem*> item);
void putFiltered(
	not_null<HistoryItem*> item,
	const Data::Group *group,
	bool res,
	const std::shared_ptr<const Cache> &matchedCache);

void invalidate(not_null<HistoryItem*> item);

// Forgets everything remembered for a session that is going away.
void invalidateSession(uint64 sessionId);

}
