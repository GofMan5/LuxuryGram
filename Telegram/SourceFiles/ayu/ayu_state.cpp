// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ayu_state.h"

#include "ayu/ayu_settings.h"
#include "main/main_session.h"

namespace LuxuryState {

std::unordered_map<PeerId, std::unordered_set<MsgId>> hiddenMessages;
std::size_t hiddenMessagesCount = 0;
base::weak_ptr<Main::Session> disableGhostModeOnStoryCloseSession;

void hide(PeerId peerId, MsgId messageId) {
	const auto existing = hiddenMessages.find(peerId);
	if (existing != end(hiddenMessages)
		&& existing->second.contains(messageId)) {
		return;
	}

	// ponytail: bounded session cache; persist IDs if 65K hides per launch becomes real.
	constexpr auto kMaxHiddenMessages = std::size_t(65'536);
	if (hiddenMessagesCount >= kMaxHiddenMessages) {
		const auto peer = begin(hiddenMessages);
		peer->second.erase(begin(peer->second));
		if (peer->second.empty()) {
			hiddenMessages.erase(peer);
		}
		--hiddenMessagesCount;
	}
	hiddenMessages[peerId].insert(messageId);
	++hiddenMessagesCount;
}

void hide(not_null<HistoryItem*> item) {
	hide(item->history()->peer->id, item->id);
}

bool isHidden(PeerId peerId, MsgId messageId) {
	const auto it = hiddenMessages.find(peerId);
	if (it != hiddenMessages.end()) {
		return it->second.contains(messageId);
	}
	return false;
}

bool isHidden(not_null<HistoryItem*> item) {
	return isHidden(item->history()->peer->id, item->id);
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
