/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_threads.h"

#include <thread>

#if defined _WIN32
#include <windows.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>
#elif defined __APPLE__ // _WIN32
#include <sys/sysctl.h>
#elif defined __linux__ // _WIN32 || __APPLE__
#include <sched.h>

#include <fstream>
#include <set>
#include <string>
#endif // _WIN32 || __APPLE__ || __linux__

#ifdef CRL_USE_WINAPI
#include <concrt.h>
#endif // CRL_USE_WINAPI

namespace Core::Threads {
namespace {

constexpr auto kMaxLogical = 256;

[[nodiscard]] Topology Fallback() {
	const auto logical = std::max(
		1,
		static_cast<int>(std::thread::hardware_concurrency()));
	return { .logical = logical, .physical = logical };
}

#if defined _WIN32

[[nodiscard]] int Popcount(KAFFINITY mask) {
	return std::popcount(static_cast<std::uint64_t>(mask));
}

[[nodiscard]] Topology DetectSystem() {
	auto size = DWORD(0);
	GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
	if (!size) {
		return Fallback();
	}
	auto buffer = std::vector<std::byte>(size);
	const auto records = reinterpret_cast<
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data());
	if (!GetLogicalProcessorInformationEx(
			RelationProcessorCore,
			records,
			&size)) {
		return Fallback();
	}
	const auto groups = std::max(
		1,
		static_cast<int>(GetActiveProcessorGroupCount()));
	// A process mask only describes one group; with one group it is
	// exact and follows `start /affinity` and job object limits.
	auto allowed = ~KAFFINITY(0);
	if (groups == 1) {
		auto process = DWORD_PTR(0);
		auto system = DWORD_PTR(0);
		if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system)
			&& process) {
			allowed = static_cast<KAFFINITY>(process);
		}
	}
	auto cores = std::vector<CoreRecord>();
	for (auto offset = DWORD(0); offset < size;) {
		const auto record = reinterpret_cast<
			const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
				buffer.data() + offset);
		if (!record->Size) {
			break;
		}
		offset += record->Size;
		if (record->Relationship != RelationProcessorCore) {
			continue;
		}
		const auto &processor = record->Processor;
		auto logical = 0;
		for (auto i = 0; i != static_cast<int>(processor.GroupCount); ++i) {
			logical += Popcount(processor.GroupMask[i].Mask & allowed);
		}
		cores.push_back({
			.logical = logical,
			.efficiencyClass = static_cast<int>(processor.EfficiencyClass),
		});
	}
	auto result = Summarize(cores);
	if (!result.logical) {
		return Fallback();
	}
	result.groups = groups;
	if (groups > 1) {
		const auto active = static_cast<int>(
			GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
		result.logical = std::max(result.logical, active);
	}
	return result;
}

#elif defined __APPLE__ // _WIN32

[[nodiscard]] int SysctlInt(const char *name) {
	auto value = 0;
	auto length = sizeof(value);
	return (sysctlbyname(name, &value, &length, nullptr, 0) == 0)
		? value
		: 0;
}

[[nodiscard]] Topology DetectSystem() {
	const auto logical = SysctlInt("hw.activecpu");
	if (logical <= 0) {
		return Fallback();
	}
	const auto physical = SysctlInt("hw.physicalcpu");
	return {
		.logical = logical,
		.physical = (physical > 0) ? physical : logical,
		.performanceLogical = SysctlInt("hw.perflevel0.logicalcpu"),
		.efficiencyLogical = SysctlInt("hw.perflevel1.logicalcpu"),
	};
}

#elif defined __linux__ // _WIN32 || __APPLE__

[[nodiscard]] Topology DetectSystem() {
	auto set = cpu_set_t();
	CPU_ZERO(&set);
	if (sched_getaffinity(0, sizeof(set), &set) != 0) {
		return Fallback();
	}
	const auto logical = CPU_COUNT(&set);
	if (logical <= 0) {
		return Fallback();
	}
	auto siblings = std::set<std::string>();
	for (auto cpu = 0; cpu != CPU_SETSIZE; ++cpu) {
		if (!CPU_ISSET(cpu, &set)) {
			continue;
		}
		auto file = std::ifstream("/sys/devices/system/cpu/cpu"
			+ std::to_string(cpu)
			+ "/topology/thread_siblings_list");
		auto line = std::string();
		if (!std::getline(file, line)) {
			siblings.clear();
			break;
		}
		siblings.insert(line);
	}
	const auto physical = static_cast<int>(siblings.size());
	return {
		.logical = logical,
		.physical = (physical > 0) ? physical : logical,
	};
}

#else // _WIN32 || __APPLE__ || __linux__

[[nodiscard]] Topology DetectSystem() {
	return Fallback();
}

#endif // _WIN32 || __APPLE__ || __linux__

} // namespace

const Topology &Detect() {
	static const auto result = [] {
		auto topology = DetectSystem();
		topology.logical = std::clamp(topology.logical, 1, kMaxLogical);
		topology.physical = std::clamp(topology.physical, 1, topology.logical);
		return topology;
	}();
	return result;
}

const Budget &Get() {
	static const auto result = ComputeBudget(Detect());
	return result;
}

void ApplyProcessPolicy() {
#ifdef CRL_USE_WINAPI
	const auto workers = Get().concrtMinWorkers;
	if (workers <= 0) {
		return;
	}
	try {
		Concurrency::Scheduler::SetDefaultSchedulerPolicy(
			Concurrency::SchedulerPolicy(
				2,
				Concurrency::MinConcurrency,
				static_cast<unsigned int>(workers),
				Concurrency::MaxConcurrency,
				static_cast<unsigned int>(workers)));
	} catch (...) {
		// A scheduler already exists: keep its policy.
	}
#endif // CRL_USE_WINAPI
}

} // namespace Core::Threads
