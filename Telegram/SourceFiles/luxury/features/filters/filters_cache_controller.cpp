// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/features/filters/filters_cache_controller.h"

#include "luxury/data/luxury_database.h"
#include "luxury/features/filters/filters_controller.h"
#include "data/data_groups.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "rpl/event_stream.h"

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace FiltersCacheController {

std::mutex rebuildMutex;
std::mutex cacheMutex;
std::mutex filteredMessagesMutex;

rpl::event_stream<> filtersUpdateStream;

void fireUpdate() {
	filtersUpdateStream.fire({});
}

rpl::producer<> updates() {
	return filtersUpdateStream.events();
}

std::shared_ptr<const Cache> cache;

std::unordered_map<
	uint64,
	std::unordered_map<long long, std::unordered_map<int64, bool>>> filteredMessages;
std::unordered_map<uint64, std::unordered_set<BareId>> dialogsWithHiddenBlockedMessages;
std::size_t filteredMessagesCount = 0;
std::size_t hiddenBlockedDialogsCount = 0;
constexpr auto kMaxCachedFilterResults = std::size_t(65'536);

std::shared_ptr<const Cache> buildCache() {
	const auto filters = LuxuryDatabase::getAllRegexFilters();
	const auto exclusions = LuxuryDatabase::getAllFiltersExclusions();

	std::vector<HashablePattern> shared;
	std::unordered_map<ID, std::vector<ReversiblePattern>> byDialogId;

	for (const auto &filter : filters) {
		if (!filter.enabled
			|| filter.text.empty()
			|| filter.text.size() > (kMaxPatternLength * 4)) {
			continue;
		}

		int flags = UREGEX_MULTILINE;
		if (filter.caseInsensitive) flags |= UREGEX_CASE_INSENSITIVE;

		auto status = U_ZERO_ERROR;
		auto pattern = icu::RegexPattern::compile(icu::UnicodeString::fromUTF8(filter.text), flags, status);

		if (!pattern) {
			continue;
		}

		if (filter.dialogId.has_value()) {
			byDialogId[*filter.dialogId].push_back({
				std::shared_ptr<icu::RegexPattern>(pattern),
				filter.reversed,
			});
		} else {
			shared.push_back({
				filter.id,
				{
					std::shared_ptr<icu::RegexPattern>(pattern),
					filter.reversed,
				},
			});
		}
	}

	auto exclByDialogId = buildExclusions(exclusions, shared);
	auto result = std::make_shared<Cache>();
	result->sharedPatterns = std::move(shared);
	result->patternsByDialogId = std::move(byDialogId);
	result->exclusionsByDialogId = std::move(exclByDialogId);

	return result;
}

void rebuildCache() {
	std::lock_guard rebuildLock(rebuildMutex);
	auto next = buildCache();
	{
		std::lock_guard cacheLock(cacheMutex);
		std::lock_guard filteredLock(filteredMessagesMutex);
		cache = std::move(next);
		filteredMessages.clear();
		dialogsWithHiddenBlockedMessages.clear();
		filteredMessagesCount = 0;
		hiddenBlockedDialogsCount = 0;
	}
}

std::unordered_map<long long, std::unordered_set<HashablePattern, PatternHasher>> buildExclusions(
	const std::vector<RegexFilterGlobalExclusion> &exclusions,
	const std::vector<HashablePattern> &shared) {
	std::unordered_map<long long, std::unordered_set<HashablePattern, PatternHasher>> exclusionsByDialogId;

	for (const auto &exclusion : exclusions) {
		auto &exclusionSet = exclusionsByDialogId[exclusion.dialogId];

		for (const auto &filter : shared) {
			if (filter.id == exclusion.filterId) {
				exclusionSet.insert(filter);
				break;
			}
		}
	}
	return exclusionsByDialogId;
}

std::shared_ptr<const Cache> snapshot() {
	{
		std::lock_guard lock(cacheMutex);
		if (cache) {
			return cache;
		}
	}

	std::lock_guard rebuildLock(rebuildMutex);
	{
		std::lock_guard lock(cacheMutex);
		if (cache) {
			return cache;
		}
	}

	auto next = buildCache();
	std::lock_guard lock(cacheMutex);
	if (!cache) {
		cache = std::move(next);
	}
	return cache;
}

std::optional<bool> isFiltered(not_null<HistoryItem*> item) {
	std::lock_guard lock(filteredMessagesMutex);
	const auto sessionIt = filteredMessages.find(
		item->history()->session().uniqueId());
	if (sessionIt == end(filteredMessages)) {
		return std::nullopt;
	}
	const auto dialogIt = sessionIt->second.find(
		item->history()->peer->id.value);

	if (dialogIt == end(sessionIt->second)) {
		return std::nullopt;
	}

	const auto it = dialogIt->second.find(item->id.bare);
	if (it == dialogIt->second.end()) {
		return std::nullopt;
	}

	return it->second;
}

bool hasFilteredMessages(not_null<PeerData*> peer) {
	std::lock_guard lock(filteredMessagesMutex);
	const auto sessionId = peer->session().uniqueId();
	const auto hidden = dialogsWithHiddenBlockedMessages.find(sessionId);
	if (hidden != end(dialogsWithHiddenBlockedMessages)
		&& hidden->second.contains(peer->id.value)) {
		return true;
	}
	const auto sessionIt = filteredMessages.find(sessionId);
	if (sessionIt == end(filteredMessages)) {
		return false;
	}
	const auto dialogIt = sessionIt->second.find(peer->id.value);
	if (dialogIt == end(sessionIt->second)) {
		return false;
	}
	for (const auto &entry : dialogIt->second) {
		if (entry.second) {
			return true;
		}
	}
	return false;
}

void putHiddenBlockedMessage(not_null<HistoryItem*> item) {
	std::lock_guard lock(filteredMessagesMutex);
	const auto sessionId = item->history()->session().uniqueId();
	const auto peerId = item->history()->peer->id.value;
	const auto session = dialogsWithHiddenBlockedMessages.find(sessionId);
	if (session != end(dialogsWithHiddenBlockedMessages)
		&& session->second.contains(peerId)) {
		return;
	}
	// ponytail: bounded memoization; replace with LRU only if rescans are measurable.
	if (hiddenBlockedDialogsCount >= kMaxCachedFilterResults) {
		dialogsWithHiddenBlockedMessages.clear();
		hiddenBlockedDialogsCount = 0;
	}
	dialogsWithHiddenBlockedMessages[sessionId].insert(peerId);
	++hiddenBlockedDialogsCount;
}

void putFiltered(
		not_null<HistoryItem*> item,
		const Data::Group *group,
		bool res,
		const std::shared_ptr<const Cache> &matchedCache) {
	std::lock_guard cacheLock(cacheMutex);
	if (cache != matchedCache) {
		return;
	}

	std::lock_guard filteredLock(filteredMessagesMutex);
	// ponytail: bounded memoization; replace with LRU only if rescans are measurable.
	if (filteredMessagesCount >= kMaxCachedFilterResults) {
		filteredMessages.clear();
		filteredMessagesCount = 0;
	}
	const auto sessionId = item->history()->session().uniqueId();
	auto &dialog = filteredMessages[sessionId][item->history()->peer->id.value];
	filteredMessagesCount += dialog.insert_or_assign(item->id.bare, res).second;
	if (group && res) {
		for (const auto& groupItem : group->items) {
			filteredMessagesCount += dialog.insert_or_assign(
				groupItem->id.bare,
				true).second;
		}
	}
}

void invalidateSingle(not_null<HistoryItem*> item) {
	const auto sessionIt = filteredMessages.find(
		item->history()->session().uniqueId());
	if (sessionIt == end(filteredMessages)) {
		return;
	}
	const auto dialogIt = sessionIt->second.find(
		item->history()->peer->id.value);

	if (dialogIt == end(sessionIt->second)) {
		return;
	}

	const auto it = dialogIt->second.find(item->id.bare);
	if (it == dialogIt->second.end()) {
		return;
	}

	dialogIt->second.erase(it);
	--filteredMessagesCount;
	if (dialogIt->second.empty()) {
		sessionIt->second.erase(dialogIt);
		if (sessionIt->second.empty()) {
			filteredMessages.erase(sessionIt);
		}
	}
}

void invalidate(not_null<HistoryItem*> item) {
	std::lock_guard lock(filteredMessagesMutex);
	if (const auto group = item->history()->owner().groups().find(item)) {
		for (const auto& groupItem : group->items) {
			invalidateSingle(groupItem);
		}
	} else {
		invalidateSingle(item);
	}
}

}
