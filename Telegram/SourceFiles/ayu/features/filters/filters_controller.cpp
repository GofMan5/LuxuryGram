// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026

#include "ayu/features/filters/filters_controller.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/filters/filters_cache_controller.h"
#include "ayu/features/filters/filters_utils.h"
#include "ayu/utils/telegram_helpers.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "unicode/regex.h"

#include <QElapsedTimer>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace FiltersController {

std::unordered_map<uint64, std::unordered_set<long long>> showingFilteredMessages;

constexpr auto kRegexEvaluationBudgetMs = 25;
constexpr auto kRegexStackLimitBytes = 1024 * 1024;

bool filterBlocked(const not_null<HistoryItem*> item) {
	if (item->from() != item->history()->peer) {
		if (isBlocked(item)) {
			return true;
		}
	}
	if (const auto bot = item->viaBot()) {
		if (isBlocked(bot)) {
			return true;
		}
	}
	return false;
}

std::optional<bool> isFiltered(
		const QString &str,
		long long dialogId,
		const std::shared_ptr<const FiltersCacheController::Cache> &cache) {
	if (str.isEmpty()) {
		return std::nullopt;
	}

	const auto icuStr = icu::UnicodeString(reinterpret_cast<const UChar*>(str.constData()), str.length());
	auto timer = QElapsedTimer();
	timer.start();

	const auto matches = [&](const ReversiblePattern &pattern)
	{
		const auto remaining = kRegexEvaluationBudgetMs - timer.elapsed();
		if (remaining <= 0) {
			return false;
		}
		UErrorCode status = U_ZERO_ERROR;

		const auto matcher = std::unique_ptr<icu::RegexMatcher>(pattern.pattern->matcher(icuStr, status));
		if (U_FAILURE(status) || !matcher) {
			LOG(("FILTER FAILED: %1").arg(u_errorName(status)));
			return false;
		}
		matcher->setTimeLimit(int32_t(remaining), status);
		matcher->setStackLimit(kRegexStackLimitBytes, status);
		if (U_FAILURE(status)) {
			LOG(("FILTER LIMIT FAILED: %1").arg(u_errorName(status)));
			return false;
		}

		const auto match = matcher->find(status);
		if (U_FAILURE(status)) {
			if (status != U_REGEX_TIME_OUT
				&& status != U_REGEX_STACK_OVERFLOW) {
				LOG(("FILTER MATCH FAILED: %1").arg(u_errorName(status)));
			}
			return false;
		}
		const auto reversed = pattern.reversed;

		if ((!reversed && match) || (reversed && !match)) {
			return true;
		}
		return false;
	};

	if (const auto i = cache->patternsByDialogId.find(dialogId); i != cache->patternsByDialogId.end()) {
		for (const auto &pattern : i->second) {
			if (timer.hasExpired(kRegexEvaluationBudgetMs)) {
				break;
			}
			if (matches(pattern)) {
				return true;
			}
		}
	}

	const auto exclusions = cache->exclusionsByDialogId.find(dialogId);
	if (!cache->sharedPatterns.empty()) {
		for (const auto &pattern : cache->sharedPatterns) {
			if (timer.hasExpired(kRegexEvaluationBudgetMs)) {
				break;
			}
			if (exclusions != cache->exclusionsByDialogId.end() && exclusions->second.contains(pattern)) {
				continue;
			}
			if (matches(pattern.pattern)) {
				return true;
			}
		}
	}
	return false;
}

bool isEnabled(not_null<PeerData*> peer) {
	const auto &settings = LuxurySettings::getInstance();
	return settings.filtersEnabled() && (settings.filtersEnabledInChats() || peer->isBroadcast());
}

bool isBlocked(const not_null<HistoryItem*> item) {
	const auto &settings = LuxurySettings::getInstance();

	auto shadowBanMatched = false;
	const auto blocked = [&]() -> bool
	{
		const auto isShadowBanned = [&](PeerData *peer) {
			return peer
				&& (peer->isUser() || peer->isBroadcast())
				&& settings.isShadowBanned(getDialogIdFromPeer(peer));
		};

		if (isShadowBanned(item->from())
			&& item->from()->id != item->history()->peer->id) {
			shadowBanMatched = true;
			return true;
		}

		if (item->from()->isUser()
			&& item->from()->asUser()->isBlocked()) {
			// don't hide messages if it's a dialog with blocked user
			return item->from()->asUser()->id != item->history()->peer->id;
		}

		if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
			if (const auto originalSender = forwarded->originalSender) {
				const auto originalShadowBanned = isShadowBanned(originalSender);
				if (originalShadowBanned
					|| (originalSender->isUser()
						&& originalSender->asUser()->isBlocked())) {
					shadowBanMatched = originalShadowBanned;
					return true;
				}
			}
		}
		return false;
	}();

	return settings.filtersEnabled()
		&& (shadowBanMatched || settings.hideFromBlocked())
		&& blocked;
}

bool isBlocked(const not_null<PeerData*> peer) {
	const auto &settings = LuxurySettings::getInstance();
	return settings.filtersEnabled() &&
	(
		(peer->isUser() && peer->asUser()->isBlocked() && settings.hideFromBlocked()) ||
		((peer->isUser() || peer->isBroadcast()) && settings.isShadowBanned(getDialogIdFromPeer(peer)))
	);
}

bool filtered(const not_null<HistoryItem*> item) {
	const auto sessionId = item->history()->session().uniqueId();
	const auto shown = showingFilteredMessages.find(sessionId);
	if (shown != end(showingFilteredMessages)
		&& shown->second.contains(item->history()->peer->id.value)) {
		return false;
	}

	const auto &settings = LuxurySettings::getInstance();
	if (!settings.filtersEnabled()) {
		return false;
	}

	if (item->out()) {
		return false;
	}

	if (filterBlocked(item)) {
		FiltersCacheController::putHiddenBlockedMessage(item);
		return true;
	}

	if (!isEnabled(item->history()->peer)) return false;

	const auto cached = FiltersCacheController::isFiltered(item);
	if (cached) {
		return *cached;
	}
	const auto group = item->history()->owner().groups().find(item);
	const auto cache = FiltersCacheController::snapshot();
	const auto res = isFiltered(
		FilterUtils::extractAllText(item, group),
		getDialogIdFromPeer(item->history()->peer),
		cache);

	// sometimes item has empty text.
	// so we cache result only if
	// processed item is filterable
	if (res) {
		FiltersCacheController::putFiltered(item, group, *res, cache);
		return *res;
	}
	return false;
}

std::optional<bool> filteredMessagesShown(not_null<PeerData*> peer) {
	const auto sessionId = peer->session().uniqueId();
	const auto shown = showingFilteredMessages.find(sessionId);
	const auto showing = shown != end(showingFilteredMessages)
		&& shown->second.contains(peer->id.value);
	if (!showing
		&& !FiltersCacheController::hasFilteredMessages(peer)) {
		return std::nullopt;
	}
	return showing;
}

void toggleFilteredMessagesShown(not_null<PeerData*> peer) {
	const auto sessionId = peer->session().uniqueId();
	auto &shown = showingFilteredMessages[sessionId];
	if (shown.contains(peer->id.value)) {
		shown.erase(peer->id.value);
		if (shown.empty()) {
			showingFilteredMessages.erase(sessionId);
		}
	} else {
		shown.insert(peer->id.value);
	}
	FiltersCacheController::fireUpdate();
}

void invalidate(not_null<HistoryItem*> item) {
	FiltersCacheController::invalidate(item);
}

}
