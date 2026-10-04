// Standalone check of the production helper; no Qt or profile required.
#include "luxury/ui/watcher/watcher_revisions.h"

#include <cassert>
#include <iostream>
#include <string>

struct Snapshot {
	long long messageId = 0;
	std::string text;
};

int main() {
	using LuxuryWatcher::RevisionAfterStates;
	assert(RevisionAfterStates(std::vector<Snapshot>()).empty());
	const auto single = std::vector<Snapshot>{{ 1, "original" }};
	assert(RevisionAfterStates(single).front() == nullptr);

	// Pre-edit snapshots for A -> B -> A -> C; current C is not stored.
	const auto rows = std::vector<Snapshot>{
		{ 1, "A" }, { 2, "other" }, { 1, "B" }, { 1, "A" },
	};
	const auto after = RevisionAfterStates(rows);
	assert(after.size() == rows.size());
	assert(after[0] == nullptr);
	assert(after[1] == nullptr);
	assert(after[2] == &rows[0]);
	assert(after[3] == &rows[2]);
	assert(rows[3].text == after[2]->text); // reversions stay separate

	const auto big = std::vector<Snapshot>(200, Snapshot{ 7, "paragraph\nemoji" });
	const auto paired = RevisionAfterStates(big);
	for (auto i = std::size_t(1); i != big.size(); ++i) {
		assert(paired[i] == &big[i - 1]);
	}
	std::cout << "Revision snapshots: PASS (empty, single, interleaved, revert, 200 rows)\n";
}
