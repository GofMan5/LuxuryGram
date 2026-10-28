// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include "luxury/plugins/core/plugin_permissions.h"
#include "luxury/plugins/core/plugin_version.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Luxury::Plugins {

inline constexpr auto kMaxPluginFiles = 200;
inline constexpr auto kMaxPluginBytes = std::int64_t(20) * 1024 * 1024;
inline constexpr auto kMaxManifestBytes = std::size_t(256) * 1024;

enum class Runtime {
	Js,
};

enum class SettingType {
	String,
	Bool,
	Int,
	Choice,
};

struct SettingDescriptor {
	std::string key;
	SettingType type = SettingType::String;
	std::string title;
	std::string defaultString; // String and Choice.
	bool defaultBool = false;
	std::int64_t defaultInt = 0;
	std::optional<std::int64_t> min;
	std::optional<std::int64_t> max;
	std::vector<std::string> options; // Choice.
};

struct Manifest {
	std::string id;
	std::string name;
	std::string version;
	std::string description;
	std::string author;
	int api = 0;
	Runtime runtime = Runtime::Js;
	std::string main;
	std::vector<std::string> files;
	PermissionSet permissions;
	std::vector<std::string> network; // Lowercase, in manifest order.
	std::vector<SettingDescriptor> settings;
	std::string minClient;
	std::string homepage;
};

[[nodiscard]] bool IsValidPluginId(std::string_view id);

// Rejects empty paths and segments, "." and "..", absolute paths, '\',
// ':' (drive letters, streams), NUL and control characters, characters
// Windows cannot store (<>"|?*), segments ending in '.' or ' ', and
// Windows device names (con, nul, com1, ...), with or without extension.
[[nodiscard]] bool IsSafeRelativePath(std::string_view path);

[[nodiscard]] Result<Manifest> ParseManifest(std::string_view json);

} // namespace Luxury::Plugins
