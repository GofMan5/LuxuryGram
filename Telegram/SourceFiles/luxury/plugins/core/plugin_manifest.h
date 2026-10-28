// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include "luxury/plugins/core/plugin_permissions.h"
#include "luxury/plugins/core/plugin_version.h"

#include <cstdint>
#include <map>
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
	Select, // Choice under its documented name.
	Number,
	Color,
	Secret, // A string the client never logs; the UI masks it.
};

// "locales": { "ru": { "name": "...", "description": "..." } }.
struct LocalizedText {
	std::string name;
	std::string description;
};
using Locales = std::map<std::string, LocalizedText, std::less<>>;

struct SettingDescriptor {
	std::string key;
	SettingType type = SettingType::String;
	std::string title;
	std::string description;
	std::string placeholder;
	Locales locales; // "name" translates the title.
	std::string defaultString; // String, Choice, Select, Color, Secret.
	bool defaultBool = false;
	std::int64_t defaultInt = 0;
	std::optional<std::int64_t> min; // Int.
	std::optional<std::int64_t> max;
	double defaultNumber = 0.;
	std::optional<double> minNumber; // Number.
	std::optional<double> maxNumber;
	std::optional<double> step; // Number, positive; a UI hint.
	std::vector<std::string> options; // Choice and Select.
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
	Locales locales;
};

[[nodiscard]] bool IsValidPluginId(std::string_view id);

// "#RRGGBB", hex digits in either case.
[[nodiscard]] bool IsValidColor(std::string_view color);

// Rejects empty paths and segments, "." and "..", absolute paths, '\',
// ':' (drive letters, streams), NUL and control characters, characters
// Windows cannot store (<>"|?*), segments ending in '.' or ' ', and
// Windows device names (con, nul, com1, ...), with or without extension.
[[nodiscard]] bool IsSafeRelativePath(std::string_view path);

[[nodiscard]] Result<Manifest> ParseManifest(std::string_view json);

} // namespace Luxury::Plugins
