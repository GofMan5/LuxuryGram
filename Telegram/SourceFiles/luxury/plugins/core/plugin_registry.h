// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include "luxury/plugins/core/plugin_permissions.h"
#include "luxury/plugins/core/plugin_source.h"
#include "luxury/plugins/core/plugin_version.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Luxury::Plugins {

inline constexpr auto kRegistryFormat = 1;

struct PluginSource {
	std::string owner;
	std::string repo;
	std::string subdir;
	Channel channel = Channel::Release;
	std::string ref; // Release tag or commit SHA that is installed.
	std::string label; // Branch name for the branch channel.
};

struct InstalledPlugin {
	std::string id;
	std::string name;
	std::string version;
	std::string description;
	std::string author;
	std::string homepage;
	PermissionSet requested;
	std::vector<std::string> network;
	PluginSource source;
	PermissionSet granted;
	bool enabled = false;
	std::int64_t installedAt = 0; // Unix seconds.
	std::string lastError;
};

struct Registry {
	std::vector<InstalledPlugin> plugins;
	// Entries dropped while reading, "plugins[3]: bad id" and so on.
	std::vector<std::string> skipped;
};

// Never fails on a damaged entry: it lands in skipped instead. Fails only
// when the document itself is not a registry object.
[[nodiscard]] Result<Registry> ParseRegistry(std::string_view json);

// Plugins with an invalid id are not written.
[[nodiscard]] std::string SerializeRegistry(const Registry &registry);

// True when requested holds a permission the user has not granted.
[[nodiscard]] bool NeedsNewConsent(
	PermissionSet granted,
	PermissionSet requested);

} // namespace Luxury::Plugins
