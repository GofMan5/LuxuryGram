// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace LuxuryFeatures::Watch {

// Server observations only: UI activity inference must never enter this state.
class PresenceTracker final {
public:
	[[nodiscard]] std::optional<bool> observe(bool online, bool enabled, int at) {
		if (!enabled) {
			reset();
			return std::nullopt;
		}
		if (_online == online) {
			return std::nullopt;
		}
		const auto first = !_online.has_value();
		_online = online;
		_start = online ? std::optional<int>(at) : std::nullopt;
		return (first && !online) ? std::nullopt : std::optional<bool>(online);
	}

	void reset() {
		_online = std::nullopt;
		_start = std::nullopt;
	}

	[[nodiscard]] std::optional<int> start() const {
		return _start;
	}

private:
	std::optional<bool> _online;
	std::optional<int> _start;

};

template <typename Record>
[[nodiscard]] bool SameRecordIds(
		const std::vector<Record> &a,
		const std::vector<Record> &b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (auto i = std::size_t(0); i != a.size(); ++i) {
		if (a[i].fakeId != b[i].fakeId) {
			return false;
		}
	}
	return true;
}

struct OnlineSession {
	std::optional<int> start;
	std::optional<int> end;
	long long startId = 0;
};

// Insertion order, newest first. Repeated starts can cross a restart or a
// tracking pause: keep the previous interval incomplete, never bridge the gap.
template <typename Event>
[[nodiscard]] std::vector<OnlineSession> PairOnlineSessions(
		const std::vector<Event> &events) {
	auto result = std::vector<OnlineSession>();
	result.reserve(events.size());
	auto start = std::optional<int>();
	auto startId = 0LL;
	for (auto i = events.rbegin(); i != events.rend(); ++i) {
		if (i->online) {
			if (start) {
				result.push_back({ start, std::nullopt, startId });
			}
			start = i->at;
			startId = i->fakeId;
		} else {
			result.push_back({ start, i->at, startId });
			start = std::nullopt;
			startId = 0;
		}
	}
	if (start) {
		result.push_back({ start, std::nullopt, startId });
	}
	return result;
}

} // namespace LuxuryFeatures::Watch
