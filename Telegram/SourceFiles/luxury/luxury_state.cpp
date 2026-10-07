// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/luxury_state.h"

#include "luxury/luxury_settings.h"
#include "main/main_session.h"

#include <unordered_map>
#include <unordered_set>

namespace LuxuryState {

namespace {

std::unordered_map<
	uint64,
	std::unordered_map<PeerId, std::unordered_set<MsgId>>> hiddenMessages;
std::size_t hiddenMessagesCount = 0;
base::weak_ptr<Main::Session> disableGhostModeOnStoryCloseSession;

} // namespace

void hide(not_null<HistoryItem*> item) {
	const auto sessionId = item->history()->session().uniqueId();
	const auto peerId = item->history()->peer->id;
	const auto messageId = item->id;

	const auto session = hiddenMessages.find(sessionId);
	if (session != end(hiddenMessages)) {
		const auto existing = session->second.find(peerId);
		if (existing != end(session->second)
			&& existing->second.contains(messageId)) {
			return;
		}
	}

	// ponytail: bounded session cache; persist IDs if 65K hides per launch becomes real.
	constexpr auto kMaxHiddenMessages = std::size_t(65'536);
	if (hiddenMessagesCount >= kMaxHiddenMessages) {
		const auto firstSession = begin(hiddenMessages);
		const auto peer = begin(firstSession->second);
		peer->second.erase(begin(peer->second));
		if (peer->second.empty()) {
			firstSession->second.erase(peer);
			if (firstSession->second.empty()) {
				hiddenMessages.erase(firstSession);
			}
		}
		--hiddenMessagesCount;
	}
	hiddenMessages[sessionId][peerId].insert(messageId);
	++hiddenMessagesCount;
}

bool isHidden(not_null<HistoryItem*> item) {
	const auto session = hiddenMessages.find(
		item->history()->session().uniqueId());
	if (session == end(hiddenMessages)) {
		return false;
	}
	const auto peer = session->second.find(item->history()->peer->id);
	if (peer != end(session->second)) {
		return peer->second.contains(item->id);
	}
	return false;
}

void setDisableGhostModeOnStoryClose(Main::Session *session) {
	disableGhostModeOnStoryCloseSession = session
		? base::make_weak(session)
		: base::weak_ptr<Main::Session>();
}

void disableGhostModeOnStoryClose(Main::Session *session) {
	const auto current = disableGhostModeOnStoryCloseSession.get();
	if (current != session) {
		return;
	}
	disableGhostModeOnStoryCloseSession = {};
	if (current) {
		LuxurySettings::ghost(current).setGhostModeEnabled(false);
	}
}

}
