// Focused production-helper checks; no Qt, account or real profile.
#include "luxury/features/watch/presence_history.h"

#include <cassert>
#include <iostream>

using namespace LuxuryFeatures::Watch;

struct Event {
	long long fakeId = 0;
	bool online = false;
	int at = 0;
};

int main() {
	PresenceTracker tracker;
	assert(!tracker.observe(false, true, 1)); // Initial offline is not a departure.
	assert(tracker.observe(true, true, 2) == true);
	assert(tracker.start() == 2);
	assert(!tracker.observe(true, true, 3)); // Renewed server expiry, no duplicate.
	assert(tracker.start() == 2);
	assert(tracker.observe(false, true, 4) == false);
	assert(!tracker.start());
	assert(!tracker.observe(false, true, 5));
	assert(!tracker.observe(true, false, 6)); // Disabled/locked observation.
	assert(!tracker.start());
	assert(!tracker.observe(false, true, 7));
	assert(tracker.observe(true, true, 8) == true);
	tracker.reset(); // Lock, opt-out, clear and restart share reset semantics.
	assert(!tracker.start());
	assert(tracker.observe(true, true, 8) == true); // Same-second resume.

	assert(PairOnlineSessions(std::vector<Event>()).empty());
	const auto complete = PairOnlineSessions(std::vector<Event>{
		{ 2, false, 11 }, { 1, true, 10 } });
	assert(complete.size() == 1 && complete[0].start == 10 && complete[0].end == 11);
	const auto partial = PairOnlineSessions(std::vector<Event>{ { 1, false, 10 } });
	assert(partial.size() == 1 && !partial[0].start && partial[0].end == 10);
	const auto gap = PairOnlineSessions(std::vector<Event>{
		{ 3, false, 30 }, { 2, true, 20 }, { 1, true, 10 } });
	assert(gap.size() == 2 && gap[0].start == 10 && !gap[0].end);
	assert(gap[1].start == 20 && gap[1].end == 30);
	const auto sameSecond = PairOnlineSessions(std::vector<Event>{
		{ 3, true, 10 }, { 2, false, 10 }, { 1, true, 10 } });
	assert(sameSecond.size() == 2 && sameSecond[0].end == 10 && !sameSecond[1].end);
	assert(sameSecond[0].startId == 1 && sameSecond[1].startId == 3);
	const auto clockStep = PairOnlineSessions(std::vector<Event>{
		{ 2, false, 9 }, { 1, true, 10 } });
	assert(clockStep.size() == 1 && clockStep[0].start == 10 && clockStep[0].end == 9);
	const auto repeatedOffline = PairOnlineSessions(std::vector<Event>{
		{ 3, false, 30 }, { 2, false, 20 }, { 1, true, 10 } });
	assert(repeatedOffline.size() == 2 && !repeatedOffline[1].start);
	const std::vector<Event> a = { { 1, true, 10 } };
	assert(SameRecordIds(a, a));
	assert(!SameRecordIds(a, std::vector<Event>{ { 2, true, 10 } }));
	assert(!SameRecordIds(a, std::vector<Event>{}));
	std::cout << "Presence helpers: PASS (dedup, pause/lock/reset, gaps, partial history, same-second IDs, clock steps)\n";
}
