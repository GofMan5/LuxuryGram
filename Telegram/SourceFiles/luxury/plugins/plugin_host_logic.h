// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

// Qt-free decisions of the plugin host, compiled by
// tools/tests/plugin_host_regressions.py without the client.

#include "luxury/plugins/core/plugin_manifest.h"
#include "luxury/plugins/core/plugin_permissions.h"
#include "luxury/plugins/core/plugin_source.h"
#include "luxury/plugins/core/plugin_version.h"
#include "luxury/plugins/core/tl_schema.h"
#include "luxury/libs/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Luxury::Plugins {

inline constexpr auto kStorageQuota = std::size_t(5) * 1024 * 1024;
inline constexpr auto kFetchLimit = std::int64_t(10) * 1024 * 1024;
inline constexpr auto kInflateLimit = std::size_t(32) * 1024 * 1024;
inline constexpr auto kMaxRedirects = 5;
inline constexpr auto kMaxTimers = 1000;
inline constexpr auto kMaxOverruns = 3;
inline constexpr auto kOverrunSpanMs = std::int64_t(10'000);
inline constexpr auto kCpuSpanMs = std::int64_t(5'000);
inline constexpr auto kCpuSpanBudgetMs = std::int64_t(500);
inline constexpr auto kMinInterval = 10.;
inline constexpr auto kMinTimeout = 4.;
inline constexpr auto kMaxInFlight = 16;
inline constexpr auto kMaxSettingWatchers = 64;
inline constexpr auto kLogLimit = 500;
inline constexpr auto kMaxLogText = std::size_t(4096);

// http.fetch: HTTPS only. port is the URL's, -1 when it names none. Port
// 443 (or none) needs a "host" entry, any other port a "host:port" one.
[[nodiscard]] inline bool FetchAllowed(
		std::string_view scheme,
		std::string_view host,
		int port,
		const std::vector<std::string> &network) {
	if (scheme != "https") {
		return false;
	}
	const auto suffix = ':' + std::to_string(port);
	auto bare = std::vector<std::string>();
	auto ported = std::vector<std::string>();
	for (const auto &entry : network) {
		const auto colon = entry.rfind(':');
		if (colon == std::string::npos) {
			bare.push_back(entry);
		} else if (port > 0 && std::string_view(entry).substr(colon) == suffix) {
			ported.push_back(entry.substr(0, colon));
		}
	}
	return ((port < 0 || port == 443) && HostMatches(bare, host))
		|| HostMatches(ported, host);
}

// timer.set: a repeating timer fires at most every 10 ms, a single one
// waits at least 4 ms, like browsers clamp them.
[[nodiscard]] inline int TimerDelay(const nlohmann::json &delay, bool repeat) {
	const auto low = repeat ? kMinInterval : kMinTimeout;
	const auto ms = delay.is_number() ? delay.get<double>() : 0.;
	return (ms >= low) // False for NaN as well.
		? int(std::min(ms, double(1 << 30)))
		: int(low);
}

// JS time a plugin spends on the main thread. It is stopped after more
// than 500 ms within any 5 s, or after 3 slice overruns within 10 s.
struct CpuBudget {
	// Engine entries of the last kCpuSpanMs: { ended at, took }.
	std::vector<std::pair<std::int64_t, std::int64_t>> slices;
	std::vector<std::int64_t> overruns;

	// One engine entry that ended at now (ms) and took elapsed (ms).
	// True when the plugin must be stopped: a rolling sum, so no idle
	// pause lets a busy plugin start over.
	[[nodiscard]] bool record(
			std::int64_t now,
			std::int64_t elapsed,
			bool overrun) {
		slices.emplace_back(now, std::max(elapsed, std::int64_t(0)));
		std::erase_if(slices, [&](const auto &slice) {
			return now - slice.first >= kCpuSpanMs;
		});
		if (overrun) {
			overruns.push_back(now);
		}
		std::erase_if(overruns, [&](std::int64_t at) {
			return now - at >= kOverrunSpanMs;
		});
		auto used = std::int64_t(0);
		for (const auto &[at, took] : slices) {
			used += took;
		}
		return (overruns.size() >= std::size_t(kMaxOverruns))
			|| (used > kCpuSpanBudgetMs);
	}
};

// Qt::Key and Qt::KeyboardModifier values, checked against Qt in
// plugin_hooks.cpp; this header stays Qt-free.
inline constexpr auto kKeyEscape = 0x01000000;
inline constexpr auto kKeyReturn = 0x01000004;
inline constexpr auto kKeyEnter = 0x01000005;
inline constexpr auto kShiftModifier = 0x02000000;
inline constexpr auto kControlModifier = 0x04000000;
inline constexpr auto kModifierMask = 0xFE000000u;

// Keys a plugin hotkey never takes: Enter and Esc with any modifiers, and
// Ctrl(+Shift) + C, V, X, A, Z, Y. combined is QKeyCombination::toCombined.
[[nodiscard]] inline bool HotkeyReserved(int combined) {
	const auto bits = static_cast<std::uint32_t>(combined);
	const auto key = int(bits & ~kModifierMask);
	const auto modifiers = bits & kModifierMask;
	if (key == kKeyEscape || key == kKeyReturn || key == kKeyEnter) {
		return true;
	}
	const auto editing = (key > 0 && key < 0x80)
		&& std::string_view("CVXAZY").find(char(key))
			!= std::string_view::npos;
	return editing
		&& ((modifiers & ~std::uint32_t(kShiftModifier))
			== std::uint32_t(kControlModifier));
}

// http.fetch and api.call: requests a plugin may wait for at once.
[[nodiscard]] inline bool InFlightAllowed(int inFlight) {
	return inFlight < kMaxInFlight;
}

// Log lines and lastError: at most 4 KiB of UTF-8, cut on a character
// boundary and marked with an ellipsis.
[[nodiscard]] inline std::string TruncateText(std::string text) {
	if (text.size() <= kMaxLogText) {
		return text;
	}
	constexpr auto kMark = std::string_view("...");
	auto size = kMaxLogText - kMark.size();
	while (size > 0
		&& (static_cast<unsigned char>(text[size]) & 0xC0) == 0x80) {
		--size; // text[size] continues a character: cut before its lead.
	}
	text.resize(size);
	text += kMark;
	return text;
}

// The storage object as written to data/storage.json.
[[nodiscard]] inline bool StorageFits(const nlohmann::json &storage) {
	return storage.dump().size() <= kStorageQuota;
}

// One plugin's beforeSend answer folded into the chain: false cancels,
// a string replaces the text, anything else leaves it.
struct SendFold {
	std::string text;
	bool changed = false;
	bool cancelled = false;
};

inline void FoldBeforeSend(SendFold &fold, const nlohmann::json &answer) {
	if (fold.cancelled) {
		return;
	} else if (answer.is_boolean() && !answer.get<bool>()) {
		fold.cancelled = true;
	} else if (answer.is_string()) {
		auto text = answer.get<std::string>();
		if (text != fold.text) {
			fold.text = std::move(text);
			fold.changed = true;
		}
	}
}

// "/name args" typed into the message field.
struct ParsedCommand {
	std::string name;
	std::string args;
};

[[nodiscard]] inline bool IsCommandName(std::string_view name) {
	return !name.empty()
		&& (name.size() <= 32)
		&& std::all_of(name.begin(), name.end(), [](char c) {
			return (c >= 'a' && c <= 'z')
				|| (c >= '0' && c <= '9')
				|| (c == '_');
		});
}

[[nodiscard]] inline std::optional<ParsedCommand> ParseLocalCommand(
		std::string_view text) {
	const auto space = [](char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\r';
	};
	if (text.empty() || text.front() != '/') {
		return std::nullopt;
	}
	const auto end = std::find_if(text.begin() + 1, text.end(), space);
	const auto name = std::string_view(
		text.data() + 1,
		std::size_t(end - text.begin() - 1));
	if (!IsCommandName(name)) {
		return std::nullopt;
	}
	auto from = end;
	auto till = text.end();
	while (from != till && space(*from)) {
		++from;
	}
	while (till != from && space(*(till - 1))) {
		--till;
	}
	return ParsedCommand{
		std::string(name),
		std::string(from, till),
	};
}

// A param that embeds a whole query (!X) would smuggle any method past
// IsMethodForbidden, so such methods are refused outright.
[[nodiscard]] inline bool HasQuery(const Tl::TypeRef &type) {
	return (type.kind == Tl::TypeRef::Kind::Query)
		|| std::any_of(type.args.begin(), type.args.end(), [](const auto &a) {
			return HasQuery(a);
		});
}

[[nodiscard]] inline bool HasQueryParam(const Tl::Combinator &method) {
	return std::any_of(
		method.params.begin(),
		method.params.end(),
		[](const Tl::Param &param) { return HasQuery(param.type); });
}

// api.call params that spend or give away the account's Stars outside
// payments.*: paid messages and flood skips, suggested post offers,
// refunds of paid messages, and the prices other users pay to the account.
inline constexpr std::string_view kPaidParams[] = {
	"allow_paid_stars",
	"allow_paid_floodskip",
	"suggested_post",
	"send_paid_messages_stars",
	"paid_enabled",
	"refund_charged",
};

// True if any object key at any depth names a paid param; the TL codec
// matches keys exactly, so an exact compare covers every spelling it takes.
[[nodiscard]] inline bool HasPaidParam(const nlohmann::json &value) {
	if (value.is_object()) {
		for (const auto &[key, inner] : value.items()) {
			if (std::find(
					std::begin(kPaidParams),
					std::end(kPaidParams),
					key) != std::end(kPaidParams)
				|| HasPaidParam(inner)) {
				return true;
			}
		}
	} else if (value.is_array()) {
		return std::any_of(value.begin(), value.end(), [](const auto &v) {
			return HasPaidParam(v);
		});
	}
	return false;
}

// Telegram's service notifications (user 777000) carry the login codes of
// the account: a plugin never sees that chat, or it could sign in elsewhere.
inline constexpr std::string_view kServiceUserId = "777000";

// True if any object at any depth has user_id 777000: the decoder writes
// a long as a decimal string, and plugins may pass it as a number. An
// updateServiceNotification has no user_id, but the client files it in
// that same chat.
[[nodiscard]] inline bool MentionsServiceUser(const nlohmann::json &value) {
	if (value.is_object()) {
		for (const auto &[key, inner] : value.items()) {
			if (key == "_"
				&& inner.is_string()
				&& inner.get<std::string>() == "updateServiceNotification") {
				return true;
			} else if (key == "user_id"
				&& ((inner.is_string()
					&& inner.get<std::string>() == kServiceUserId)
					|| (inner.is_number_integer()
						&& inner.get<std::int64_t>() == 777000)
					|| (inner.is_number_float()
						&& inner.get<double>() == 777000.))) {
				return true;
			} else if (MentionsServiceUser(inner)) {
				return true;
			}
		}
	} else if (value.is_array()) {
		return std::any_of(value.begin(), value.end(), [](const auto &v) {
			return MentionsServiceUser(v);
		});
	}
	return false;
}

// Drops every array element that mentions the service chat (its messages,
// dialogs, updates) and returns false when the rest still mentions it, so
// the caller drops the whole value.
[[nodiscard]] inline bool StripServiceUser(nlohmann::json &value) {
	if (value.is_array()) {
		std::erase_if(value.get_ref<nlohmann::json::array_t&>(), [](
				const nlohmann::json &element) {
			return MentionsServiceUser(element);
		});
	}
	if (value.is_structured()) {
		for (auto &inner : value) {
			if (!StripServiceUser(inner)) {
				return false;
			}
		}
	}
	return !MentionsServiceUser(value);
}

// The account's own phone number is half of a sign-in elsewhere: a user
// object marked self loses it at any depth, and so does updateUserPhone,
// which does not say whose number it is.
inline void HideOwnPhone(nlohmann::json &value) {
	if (value.is_object()) {
		const auto self = value.find("self");
		const auto name = value.find("_");
		if ((self != value.end() && self->is_boolean() && self->get<bool>())
			|| (name != value.end() && *name == "updateUserPhone")) {
			value.erase("phone");
		}
	}
	if (value.is_structured()) {
		for (auto &inner : value) {
			HideOwnPhone(inner);
		}
	}
}

// The same rule the consent box shows: an update widens the network
// access when it reaches a host the installed version could not.
[[nodiscard]] inline bool NetworkWidened(
		bool oldGranted,
		const std::vector<std::string> &oldHosts,
		PermissionSet newRequested,
		const std::vector<std::string> &newHosts) {
	if (!newRequested.has(Permission::Network)) {
		return false;
	} else if (!oldGranted) {
		return true;
	}
	const auto has = [&](const std::string &host) {
		return std::find(oldHosts.begin(), oldHosts.end(), host)
			!= oldHosts.end();
	};
	return !has("*") && !std::all_of(newHosts.begin(), newHosts.end(), has);
}

// Requested permissions the user has not granted yet.
[[nodiscard]] inline PermissionSet MissingPermissions(
		PermissionSet requested,
		PermissionSet granted) {
	return PermissionSet(requested.bits() & ~granted.bits());
}

[[nodiscard]] inline PermissionSet StoredGrant(
		PermissionSet requested,
		PermissionSet granted) {
	return PermissionSet(requested.bits() & granted.bits());
}

// commitInstall: a fresh install, new permissions or wider hosts need
// the consent box first.
[[nodiscard]] inline bool NeedsConsent(
		bool update,
		PermissionSet newPermissions,
		bool widened,
		bool consented) {
	return (!update || !newPermissions.empty() || widened) && !consented;
}

// commitInstall, against the plugin.json about to be installed: what it
// asks beyond the earlier grant must all have been shown in the consent
// box, hosts included when they widen. The grant is then
// requested ∩ (previously ∪ shown); nullopt means the plugin changed
// after it was reviewed.
[[nodiscard]] inline std::optional<PermissionSet> CommitGrant(
		PermissionSet requested,
		PermissionSet previously,
		PermissionSet shown,
		bool widened,
		const std::vector<std::string> &hosts,
		const std::vector<std::string> &shownHosts) {
	const auto seen = [&](const std::string &host) {
		return std::find(shownHosts.begin(), shownHosts.end(), host)
			!= shownHosts.end();
	};
	if (!shown.contains(MissingPermissions(requested, previously))
		|| (widened && !std::all_of(hosts.begin(), hosts.end(), seen))) {
		return std::nullopt;
	}
	return StoredGrant(
		requested,
		PermissionSet(previously.bits() | shown.bits()));
}

// True when minClient names a version newer than client. An empty
// minClient asks for nothing; ParseManifest already refused a bad one.
[[nodiscard]] inline bool ClientTooOld(
		std::string_view minClient,
		std::string_view client) {
	if (minClient.empty()) {
		return false;
	}
	const auto needed = ParseVersion(minClient);
	const auto current = ParseVersion(client);
	return !needed || !current || (*needed > *current);
}

// A manifest setting: the default value of its type.
[[nodiscard]] inline nlohmann::json SettingDefault(
		const SettingDescriptor &setting) {
	switch (setting.type) {
	case SettingType::Bool: return setting.defaultBool;
	case SettingType::Int: return setting.defaultInt;
	case SettingType::Number: return setting.defaultNumber;
	case SettingType::String:
	case SettingType::Choice:
	case SettingType::Select:
	case SettingType::Color:
	case SettingType::Secret: return setting.defaultString;
	}
	return nullptr;
}

// A value the setting can hold: its type, within min/max, a listed option,
// a #RRGGBB color. A number's step is a UI hint, not a constraint.
[[nodiscard]] inline bool SettingAccepts(
		const SettingDescriptor &setting,
		const nlohmann::json &value) {
	switch (setting.type) {
	case SettingType::Bool: return value.is_boolean();
	case SettingType::Int: {
		if (!value.is_number_integer()) {
			return false;
		}
		const auto v = value.get<std::int64_t>();
		return (!setting.min || v >= *setting.min)
			&& (!setting.max || v <= *setting.max);
	}
	case SettingType::Number: {
		if (!value.is_number()) {
			return false;
		}
		const auto v = value.get<double>();
		return std::isfinite(v)
			&& (!setting.minNumber || v >= *setting.minNumber)
			&& (!setting.maxNumber || v <= *setting.maxNumber);
	}
	case SettingType::Color:
		return value.is_string() && IsValidColor(value.get<std::string>());
	case SettingType::String:
	case SettingType::Secret: return value.is_string();
	case SettingType::Choice:
	case SettingType::Select:
		return value.is_string()
			&& std::find(
				setting.options.begin(),
				setting.options.end(),
				value.get<std::string>()) != setting.options.end();
	}
	return false;
}

// "owner/repo@v1.2.0" or "owner/repo#main (abc1234)".
[[nodiscard]] inline std::string SourceLabel(
		std::string_view owner,
		std::string_view repo,
		Channel channel,
		std::string_view ref,
		std::string_view label) {
	auto result = std::string(owner) + '/' + std::string(repo);
	return (channel == Channel::Release)
		? (result + '@' + std::string(ref))
		: (result
			+ '#'
			+ std::string(label)
			+ " ("
			+ std::string(ref.substr(0, 7))
			+ ')');
}

// The label checkUpdates offers: the tag, or the short SHA.
[[nodiscard]] inline std::string AvailableLabel(const ResolvedRef &latest) {
	return (latest.channel == Channel::Release)
		? latest.ref
		: latest.ref.substr(0, 7);
}

// Why this client cannot run the manifest, empty when it can.
[[nodiscard]] inline std::string CompatibilityError(
		const Manifest &manifest,
		std::string_view client) {
	if (manifest.runtime != Runtime::Js) {
		return "This plugin needs a runtime LuxuryGram does not have yet.";
	} else if (manifest.api > kPluginApiVersion) {
		return "This plugin needs plugin API "
			+ std::to_string(manifest.api)
			+ ", LuxuryGram "
			+ std::string(client)
			+ " has "
			+ std::to_string(kPluginApiVersion)
			+ ".";
	} else if (ClientTooOld(manifest.minClient, client)) {
		return "This plugin needs LuxuryGram "
			+ manifest.minClient
			+ " or newer, this is "
			+ std::string(client)
			+ ".";
	}
	return {};
}

// Moves staged over target: the old target is renamed aside, the staged
// directory renamed in, then the old one deleted. Any failure puts the
// old target back. Both must live on one volume. Empty on success, else
// the system message in the local 8-bit encoding. Never throws.
[[nodiscard]] inline std::string SwapDirectory(
		const std::filesystem::path &staged,
		const std::filesystem::path &target) try {
	namespace fs = std::filesystem;
	auto ec = std::error_code();
	auto old = target;
	old += ".old";
	fs::remove_all(old, ec);
	fs::create_directories(target.parent_path(), ec);
	const auto hadOld = fs::exists(target, ec);
	if (hadOld) {
		fs::rename(target, old, ec);
		if (ec) {
			return ec.message();
		}
	}
	fs::rename(staged, target, ec);
	if (ec) {
		const auto text = ec.message();
		if (hadOld) {
			fs::rename(old, target, ec);
		}
		return text;
	}
	fs::remove_all(old, ec);
	return {};
} catch (const std::exception &e) {
	// path::operator+= and friends throw on allocation or conversion.
	return e.what();
}

} // namespace Luxury::Plugins
