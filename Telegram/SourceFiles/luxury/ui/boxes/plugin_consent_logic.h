// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include "luxury/plugins/core/plugin_manifest.h"
#include "luxury/plugins/plugin_install_logic.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

// Qt-free rules of the plugin boxes, compiled by plugin_ui_regressions.py.
namespace Luxury::Plugins {

// Permission pairs that together reach further than each one alone.
enum class CombinedDanger {
	MessagesOut, // Reads messages and talks to the internet.
	AccountOut, // Calls the Telegram API and talks to the internet.
	ClipboardOut, // Reads the clipboard and talks to the internet.
};

// The pairs complete in all (what the plugin holds after the install) with
// at least one half in asked (what the box asks for now).
[[nodiscard]] inline std::vector<CombinedDanger> CombinedDangers(
		PermissionSet asked,
		PermissionSet all) {
	auto result = std::vector<CombinedDanger>();
	if (!all.has(Permission::Network)) {
		return result;
	}
	const auto pair = [&](
			CombinedDanger danger,
			std::initializer_list<Permission> sources) {
		for (const auto source : sources) {
			if (all.has(source)
				&& (asked.has(source) || asked.has(Permission::Network))) {
				result.push_back(danger);
				return;
			}
		}
	};
	pair(
		CombinedDanger::MessagesOut,
		{ Permission::MessagesRead, Permission::TelegramUpdates });
	pair(CombinedDanger::AccountOut, { Permission::TelegramApi });
	pair(CombinedDanger::ClipboardOut, { Permission::Clipboard });
	return result;
}

// Hosts of next the installed version could not reach; nothing is new
// after "*".
[[nodiscard]] inline std::vector<std::string> NewHosts(
		const std::vector<std::string> &old,
		const std::vector<std::string> &next) {
	auto result = std::vector<std::string>();
	if (std::find(old.begin(), old.end(), "*") != old.end()) {
		return result;
	}
	for (const auto &host : next) {
		if (std::find(old.begin(), old.end(), host) == old.end()) {
			result.push_back(host);
		}
	}
	return result;
}

// Codes whose error text is already localized or precise: the manifest
// field, the rate limit reset time, the conflicting plugin, the needed
// version. The others carry GitHub, Qt or file system text and are shown
// by their generic localized sentence.
[[nodiscard]] inline bool KeepsErrorDetail(ErrorCode code) {
	switch (code) {
	case ErrorCode::Manifest:
	case ErrorCode::RateLimited:
	case ErrorCode::Conflict:
	case ErrorCode::Incompatible:
	case ErrorCode::Downgrade:
	case ErrorCode::Pinned:
	case ErrorCode::UpToDate:
		return true;
	default:
		return false;
	}
}

// The entry for a language id like "pt-br": exact, then "pt"; null if none.
[[nodiscard]] inline const LocalizedText *FindLocale(
		const Locales &locales,
		std::string_view language) {
	auto key = std::string(language);
	std::transform(key.begin(), key.end(), key.begin(), [](char c) {
		return char(std::tolower(static_cast<unsigned char>(c)));
	});
	if (const auto i = locales.find(key); i != locales.end()) {
		return &i->second;
	}
	const auto dash = key.find_first_of("-_");
	if (dash == std::string::npos) {
		return nullptr;
	}
	const auto i = locales.find(std::string_view(key).substr(0, dash));
	return (i != locales.end()) ? &i->second : nullptr;
}

} // namespace Luxury::Plugins
