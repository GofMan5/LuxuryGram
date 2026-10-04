// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <cstddef>
#include <vector>

namespace LuxuryWatcher {

// The database stores pre-edit snapshots, newest first. Return the next
// saved version, not a guaranteed after-state: tracking may have had gaps.
// Keep each edit, including A -> B -> A, instead of collapsing the history.
// ponytail: quadratic scan over the bounded 200-row window; use a latest-row
// map if the dialog query becomes unbounded.
template <typename Message>
[[nodiscard]] std::vector<const Message*> RevisionAfterStates(
		const std::vector<Message> &snapshots) {
	auto result = std::vector<const Message*>(snapshots.size(), nullptr);
	for (auto i = std::size_t(0); i != snapshots.size(); ++i) {
		for (auto j = std::size_t(0); j != i; ++j) {
			if (snapshots[j].messageId == snapshots[i].messageId) {
				result[i] = &snapshots[j];
			}
		}
	}
	return result;
}

} // namespace LuxuryWatcher
