// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/luxury_state.h"

#include "luxury/luxury_settings.h"
#include "main/main_session.h"

#include <array>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace LuxuryState {

namespace {

std::unordered_map<
	uint64,
	std::unordered_map<PeerId, std::unordered_set<MsgId>>> hiddenMessages;
std::size_t hiddenMessagesCount = 0;
struct StoryGhostState {
	base::weak_ptr<Main::Session> session;
	base::weak_ptr<GhostModeAccountSettings> profile;
	std::array<bool, 5> before = {};
	std::array<bool, 5> applied = {};
	std::array<uint64, 5> revisions = {};
};
std::optional<StoryGhostState> storyGhost;

std::array<bool, 5> GhostValues(const GhostModeAccountSettings &ghost) {
	return { ghost.sendReadMessages(), ghost.sendReadStories(),
		ghost.sendOnlinePackets(), ghost.sendUploadProgress(),
		ghost.sendOfflinePacketAfterOnline() };
}

void RestoreStoryGhost() {
	if (!storyGhost) return;
	const auto state = base::take(storyGhost);
	if (!state->session || !state->profile) return;
	auto &ghost = *state->profile;
	const auto values = GhostValues(ghost);
	const auto &revisions = ghost.privacyRevisions();
	if (!ghost.sendReadMessagesLocked() && values[0] == state->applied[0] && revisions[0] == state->revisions[0])
		ghost.setSendReadMessages(state->before[0]);
	if (!ghost.sendReadStoriesLocked() && values[1] == state->applied[1] && revisions[1] == state->revisions[1])
		ghost.setSendReadStories(state->before[1]);
	if (!ghost.sendOnlinePacketsLocked() && values[2] == state->applied[2] && revisions[2] == state->revisions[2])
		ghost.setSendOnlinePackets(state->before[2]);
	if (!ghost.sendUploadProgressLocked() && values[3] == state->applied[3] && revisions[3] == state->revisions[3])
		ghost.setSendUploadProgress(state->before[3]);
	if (!ghost.sendOfflinePacketAfterOnlineLocked() && values[4] == state->applied[4] && revisions[4] == state->revisions[4])
		ghost.setSendOfflinePacketAfterOnline(state->before[4]);
}

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
	if (hiddenMessages.empty()) {
		return false;
	}
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
	RestoreStoryGhost();
	if (!session) return;
	const auto profileId = LuxurySettings::getInstance().useGlobalGhostMode()
		? uint64(0) : session->uniqueId();
	auto &ghost = LuxurySettings::ghostProfile(profileId);
	const auto before = GhostValues(ghost);
	ghost.setGhostModeEnabled(true);
	storyGhost = StoryGhostState{ base::make_weak(session), base::make_weak(&ghost), before, GhostValues(ghost), ghost.privacyRevisions() };
}

void disableGhostModeOnStoryClose(Main::Session *session) {
	if (storyGhost && storyGhost->session.get() == session) RestoreStoryGhost();
}

}
