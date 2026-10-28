/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <algorithm>

// The one CPU budget of the app: every thread pool is sized from it.
// Qt-free on purpose, so the formulas are tested without a client build.
namespace Core::Threads {

struct Topology {
	int logical = 1;
	int physical = 1;
	int performanceLogical = 0;
	int efficiencyLogical = 0;
	int groups = 1;
};

struct Budget {
	int workers = 1;
	int clipThreads = 1;
	int lottieRenderers = 1;
	int streamingVideoDecode = 1;
	int transcodeDecode = 1;
	int encoderThreads = 1;
	int mtpFilePerDirection = 1;
	int concrtMinWorkers = 0;
};

[[nodiscard]] constexpr Budget ComputeBudget(Topology t) {
	const auto logical = std::max(t.logical, 1);
	// An efficiency core counts as half a performance core.
	const auto effective = (t.efficiencyLogical > 0)
		? std::clamp(
			t.performanceLogical + t.efficiencyLogical / 2,
			1,
			logical)
		: logical;
	// One logical CPU always stays free for the GUI thread.
	const auto workers = std::max(1, effective - 1);
	return {
		.workers = workers,
		.clipThreads = std::clamp(workers / 2, 2, 16),
		.lottieRenderers = std::clamp(workers / 4, 1, 8),
		.streamingVideoDecode = std::clamp(workers / 2, 2, 16),
		.transcodeDecode = std::clamp(workers / 2, 1, 8),
		.encoderThreads = std::clamp(workers / 2, 1, 8),
		// Socket hosts, not CPU work: this many for each direction.
		.mtpFilePerDirection = std::clamp((workers + 1) / 2, 1, 4),
		// ConcRT runs one worker per CPU and does not replace a worker
		// blocked on a mutex, so below 4 CPUs one blocked job stalls all.
		.concrtMinWorkers = (logical < 4) ? 4 : 0,
	};
}

// Detected once, thread-safe; respects process affinity and cgroups.
[[nodiscard]] const Topology &Detect();
[[nodiscard]] const Budget &Get();

// Call once, first thing in main(), before anything uses crl::async.
void ApplyProcessPolicy();

} // namespace Core::Threads
