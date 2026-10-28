// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

// Qt-free install, update and recovery decisions of the plugin manager,
// compiled by tools/tests/plugin_lifecycle_regressions.py.

#include "luxury/plugins/core/plugin_source.h"
#include "luxury/plugins/core/plugin_version.h"
#include "luxury/libs/json.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Luxury::Plugins {

// Why an install, update or check failed; the text stays human readable.
enum class ErrorCode {
	None,
	Link, // The link or address is not supported.
	Network, // No HTTP answer: offline, timeout, TLS.
	RateLimited, // GitHub API rate limit, see the reset time in the text.
	NotFound, // Repository, ref or plugin.json does not exist.
	Github, // Any other GitHub answer.
	TooLarge,
	Manifest, // plugin.json is not valid.
	Incompatible, // This client cannot run the plugin.
	Conflict, // Another plugin owns the id or the repository.
	Downgrade, // Older than the installed version, needs allowDowngrade.
	Pinned, // The plugin is pinned to its installed version.
	Busy, // Being installed or checked already.
	Files, // Local files could not be written or swapped.
	UpToDate, // An update found nothing newer than the installed ref.
	Consent, // The plugin asks for access the user has not reviewed.
};

inline constexpr auto kStartingName = std::string_view(".starting");
inline constexpr auto kJournalName = std::string_view("pending.json");
inline constexpr auto kLogMirrorLines = 100;
inline constexpr auto kLogMirrorWindowMs = std::int64_t(60'000);
// Stored as lastError by a crash quarantine and shown localized: the
// registry keeps no UI language.
inline constexpr auto kQuarantinedError = std::string_view(
	"luxury:quarantined");

// --- Crash quarantine. A plugin directory holds ".starting" from right
// before its engine starts until the first event loop turn after that.
// Found at the next launch, it means the client died starting the plugin.

inline void MarkStarting(const std::filesystem::path &directory) try {
	auto ec = std::error_code();
	std::filesystem::create_directories(directory, ec);
	auto file = std::ofstream(directory / kStartingName);
	file << "1";
} catch (const std::exception &) {
}

inline void ClearStarting(const std::filesystem::path &directory) try {
	auto ec = std::error_code();
	std::filesystem::remove(directory / kStartingName, ec);
} catch (const std::exception &) {
}

// At most one sentinel at a time: a plugin started while another one's
// sentinel waits for its event loop turn takes the blame alone, so a
// crash never quarantines the plugins that started before it.
struct StartFence {
	std::filesystem::path pending;

	void mark(const std::filesystem::path &directory) {
		if (!pending.empty() && pending != directory) {
			ClearStarting(pending);
		}
		pending = directory;
		MarkStarting(directory);
	}
	void clear(const std::filesystem::path &directory) {
		ClearStarting(directory);
		if (pending == directory) {
			pending.clear();
		}
	}
};

// True when the sentinel was there; it is removed either way.
[[nodiscard]] inline bool TakeStarting(
		const std::filesystem::path &directory) try {
	auto ec = std::error_code();
	const auto path = directory / kStartingName;
	const auto found = std::filesystem::exists(path, ec);
	std::filesystem::remove(path, ec);
	return found;
} catch (const std::exception &) {
	return false;
}

// --- Swap journal. SwapWithJournal moves staged over target and leaves
// <target parent>/pending.json and <target>.old behind; the caller writes
// the registry and then calls FinishSwap. A launch that finds the journal
// calls RecoverSwap: the swap is finished when the registry already names
// the journal ref, otherwise the old files are put back.

namespace details {

[[nodiscard]] inline std::filesystem::path Suffixed(
		const std::filesystem::path &path,
		const char *suffix) {
	auto result = path;
	result += suffix;
	return result;
}

[[nodiscard]] inline std::filesystem::path JournalPath(
		const std::filesystem::path &target) {
	return target.parent_path() / kJournalName;
}

} // namespace details

// Empty on success, else the system message in the local 8-bit encoding.
// Both directories must live on one volume. rollback: staged is
// <target>.prev, so an undone swap puts those files back there. Never
// throws.
[[nodiscard]] inline std::string SwapWithJournal(
		const std::filesystem::path &staged,
		const std::filesystem::path &target,
		const std::string &ref,
		bool fresh,
		bool rollback) try {
	namespace fs = std::filesystem;
	auto ec = std::error_code();
	const auto old = details::Suffixed(target, ".old");
	const auto journal = details::JournalPath(target);
	fs::remove_all(old, ec);
	fs::create_directories(target.parent_path(), ec);
	{
		const auto text = nlohmann::json{
			{ "ref", ref },
			{ "fresh", fresh },
			{ "rollback", rollback },
		}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
		auto file = std::ofstream(journal, std::ios::binary | std::ios::trunc);
		file << text;
		file.close();
		if (!file) {
			fs::remove(journal, ec);
			return "Could not write " + std::string(kJournalName) + ".";
		}
	}
	const auto moveOld = fs::exists(target, ec);
	if (moveOld) {
		fs::rename(target, old, ec);
		if (ec) {
			const auto text = ec.message();
			fs::remove(journal, ec);
			return text;
		}
	}
	fs::rename(staged, target, ec);
	if (ec) {
		const auto text = ec.message();
		if (moveOld) {
			fs::rename(old, target, ec);
		}
		fs::remove(details::JournalPath(target), ec);
		return text;
	}
	return {};
} catch (const std::exception &e) {
	// path::operator+= and friends throw on allocation or conversion.
	return e.what();
}

// After the registry names the new version: the replaced files become
// <target>.prev for a rollback, then the journal goes. Never throws.
inline void FinishSwap(const std::filesystem::path &target) try {
	namespace fs = std::filesystem;
	auto ec = std::error_code();
	const auto old = details::Suffixed(target, ".old");
	const auto prev = details::Suffixed(target, ".prev");
	fs::remove_all(prev, ec);
	if (fs::exists(old, ec)) {
		fs::rename(old, prev, ec);
	}
	fs::remove(details::JournalPath(target), ec);
} catch (const std::exception &) {
}

enum class SwapRecovery {
	None, // No journal.
	Finished, // The registry names the new files: they stay.
	RolledBack, // The registry names the old files: they are back.
};

// registeredRef is the source ref the registry holds for this plugin,
// empty when it has no entry. Never throws.
// ponytail: a reinstall of the very same ref finishes even when the
// registry write was lost; both sides hold the same ref then.
[[nodiscard]] inline SwapRecovery RecoverSwap(
		const std::filesystem::path &target,
		const std::string &registeredRef) try {
	namespace fs = std::filesystem;
	auto ec = std::error_code();
	const auto journal = details::JournalPath(target);
	if (!fs::exists(journal, ec)) {
		return SwapRecovery::None;
	}
	auto ref = std::string();
	auto fresh = false;
	auto rollback = false;
	{
		auto file = std::ifstream(journal, std::ios::binary);
		const auto parsed = nlohmann::json::parse(file, nullptr, false);
		if (parsed.is_object()) {
			const auto i = parsed.find("ref");
			const auto j = parsed.find("fresh");
			if (i != parsed.end() && i->is_string()) {
				ref = i->get<std::string>();
			}
			const auto k = parsed.find("rollback");
			fresh = (j != parsed.end()) && j->is_boolean() && j->get<bool>();
			rollback = (k != parsed.end())
				&& k->is_boolean()
				&& k->get<bool>();
		}
	}
	const auto old = details::Suffixed(target, ".old");
	if (!ref.empty() && ref == registeredRef && fs::exists(target, ec)) {
		FinishSwap(target);
		return SwapRecovery::Finished;
	}
	if (fs::exists(old, ec)) {
		if (rollback) {
			// target holds the former <target>.prev: it goes back there.
			const auto prev = details::Suffixed(target, ".prev");
			fs::remove_all(prev, ec);
			fs::rename(target, prev, ec);
		} else {
			fs::remove_all(target, ec);
		}
		fs::rename(old, target, ec);
	} else if (fresh) {
		fs::remove_all(target, ec);
	}
	fs::remove(journal, ec);
	return SwapRecovery::RolledBack;
} catch (const std::exception &) {
	return SwapRecovery::None;
}

// --- Registry recovery.

enum class RegistryChoice {
	Main,
	Backup,
	Empty,
};

// A missing registry.json is a fresh start, not damage: the backup is used
// only when the main file exists and is broken.
[[nodiscard]] inline RegistryChoice ChooseRegistry(
		bool mainExists,
		bool mainOk,
		bool backupOk) {
	if (!mainExists) {
		return RegistryChoice::Empty;
	} else if (mainOk) {
		return RegistryChoice::Main;
	}
	return backupOk ? RegistryChoice::Backup : RegistryChoice::Empty;
}

// --- Install and update decisions.

[[nodiscard]] inline bool SameName(std::string_view a, std::string_view b) {
	const auto lower = [](char c) {
		return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
	};
	if (a.size() != b.size()) {
		return false;
	}
	for (auto i = std::size_t(); i != a.size(); ++i) {
		if (lower(a[i]) != lower(b[i])) {
			return false;
		}
	}
	return true;
}

// GitHub owner and repository names are case-insensitive.
[[nodiscard]] inline bool SameRepo(
		std::string_view aOwner,
		std::string_view aRepo,
		std::string_view bOwner,
		std::string_view bRepo) {
	return SameName(aOwner, bOwner) && SameName(aRepo, bRepo);
}

// A repository deleted and created again under the same name has a new
// id: its code is not an update of the installed plugin.
[[nodiscard]] inline bool RepoReplaced(
		std::int64_t storedId,
		std::int64_t resolvedId) {
	return storedId > 0 && resolvedId > 0 && storedId != resolvedId;
}

[[nodiscard]] inline bool AcceptUpdate(
		UpdateDecision decision,
		bool allowDowngrade) {
	return (decision != UpdateDecision::Downgrade) || allowDowngrade;
}

// Every ref/subdir split of a tree/<a>/<b>/<c> link, shortest ref first:
// a branch may have '/' in its name.
[[nodiscard]] inline std::vector<std::pair<std::string, std::string>> RefSplits(
		const std::string &ref,
		const std::string &subdir) {
	auto result = std::vector<std::pair<std::string, std::string>>();
	result.emplace_back(ref, subdir);
	auto rest = std::string_view(subdir);
	auto joined = ref;
	while (!rest.empty()) {
		const auto slash = rest.find('/');
		joined += '/';
		joined += rest.substr(0, slash);
		rest = (slash == std::string_view::npos)
			? std::string_view()
			: rest.substr(slash + 1);
		if (IsValidGitRef(joined)) {
			result.emplace_back(joined, std::string(rest));
		}
	}
	return result;
}

// --- GitHub answers.

// A non-200 answer: "GitHub: <message>" from a JSON body, otherwise the
// status. Never a JSON parse error.
[[nodiscard]] inline std::string GithubErrorText(
		int status,
		std::string_view body) {
	const auto document = nlohmann::json::parse(body, nullptr, false);
	if (document.is_object()) {
		const auto i = document.find("message");
		if (i != document.end()
			&& i->is_string()
			&& !i->get_ref<const std::string&>().empty()) {
			return "GitHub: " + i->get<std::string>();
		}
	}
	return "GitHub: HTTP " + std::to_string(status);
}

// 429, or 403 with no requests left or a Retry-After; a plain 403 is a
// permission error.
[[nodiscard]] inline bool IsRateLimited(
		int status,
		std::string_view remaining,
		std::string_view retryAfter) {
	return (status == 429)
		|| (status == 403 && (remaining == "0" || !retryAfter.empty()));
}

namespace details {

[[nodiscard]] inline std::optional<std::int64_t> ParseSeconds(
		std::string_view text) {
	if (text.empty() || text.size() > 18) {
		return std::nullopt;
	}
	auto result = std::int64_t();
	for (const auto c : text) {
		if (c < '0' || c > '9') {
			return std::nullopt;
		}
		result = result * 10 + (c - '0');
	}
	return result;
}

} // namespace details

// Unix seconds when requests may resume: Retry-After (seconds) wins over
// X-RateLimit-Reset (Unix seconds); a minute from now when neither parses.
[[nodiscard]] inline std::int64_t RateLimitResetAt(
		std::string_view reset,
		std::string_view retryAfter,
		std::int64_t now) {
	if (const auto seconds = details::ParseSeconds(retryAfter)) {
		return now + *seconds;
	} else if (const auto at = details::ParseSeconds(reset); at && *at > now) {
		return *at;
	}
	return now + 60;
}

[[nodiscard]] inline ErrorCode ClassifyStatus(int status) {
	if (status <= 0) {
		return ErrorCode::Network;
	} else if (status == 404 || status == 422) {
		return ErrorCode::NotFound;
	} else if (status == 429) {
		return ErrorCode::RateLimited;
	}
	return (status == 200) ? ErrorCode::None : ErrorCode::Github;
}

// The numeric "id" of an api.github.com/repos/<o>/<r> answer.
[[nodiscard]] inline std::optional<std::int64_t> ParseRepoId(
		std::string_view json) {
	const auto document = nlohmann::json::parse(json, nullptr, false);
	if (!document.is_object()) {
		return std::nullopt;
	}
	const auto i = document.find("id");
	return (i != document.end() && i->is_number_integer())
		? std::make_optional(i->get<std::int64_t>())
		: std::nullopt;
}

// checkUpdates: one add() per checked plugin.
struct UpdateTally {
	int available = 0;
	int failed = 0;
	ErrorCode firstCode = ErrorCode::None;
	std::string firstError;

	void add(std::string_view error, ErrorCode code, bool found) {
		if (!error.empty()) {
			if (!failed++) {
				firstError = std::string(error);
				firstCode = (code == ErrorCode::None)
					? ErrorCode::Github
					: code;
			}
		} else if (found) {
			++available;
		}
	}
};

// Plugin log lines mirrored into the client log: at most kLogMirrorLines
// per kLogMirrorWindowMs for one plugin.
struct LogBudget {
	std::int64_t windowStart = 0;
	int count = 0;

	// The first refused line of a window has count == kLogMirrorLines + 1.
	[[nodiscard]] bool allow(std::int64_t nowMs) {
		if (nowMs < windowStart
			|| nowMs - windowStart >= kLogMirrorWindowMs) {
			windowStart = nowMs;
			count = 0;
		}
		return ++count <= kLogMirrorLines;
	}
};

// A plugin log line mirrored into the client log stays one line there.
[[nodiscard]] inline std::string OneLine(std::string text) {
	for (auto &c : text) {
		if (c == '\r' || c == '\n') {
			c = ' ';
		}
	}
	return text;
}

} // namespace Luxury::Plugins
