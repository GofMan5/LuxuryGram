// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/core/plugin_manifest.h"

#include "luxury/libs/json.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Luxury::Plugins {
namespace {

using Json = nlohmann::json;

constexpr auto kMaxSettingKeyLength = std::size_t(64);
constexpr auto kMaxHostLength = std::size_t(253);
constexpr auto kMaxLocaleLength = std::size_t(16);
constexpr auto kMaxShownValue = std::size_t(64);

[[nodiscard]] Error Fail(std::string_view field, std::string_view problem) {
	return Error{ std::string(field) + ": " + std::string(problem) };
}

[[nodiscard]] std::string Indexed(std::string_view field, std::size_t i) {
	return std::string(field) + '[' + std::to_string(i) + ']';
}

[[nodiscard]] std::string Quoted(std::string_view text) {
	return '"' + std::string(text) + '"';
}

// JSON-escaped and cut, for echoing a value the author got wrong.
[[nodiscard]] std::string Shown(std::string_view text) {
	const auto cut = (text.size() > kMaxShownValue);
	auto result = Json(std::string(text.substr(0, kMaxShownValue))).dump(
		-1,
		' ',
		false,
		Json::error_handler_t::replace);
	return cut ? (result + "...") : result;
}

[[nodiscard]] char Lower(char c) {
	return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

[[nodiscard]] bool IsLowerAlnum(char c) {
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

[[nodiscard]] bool IsWindowsDevice(std::string_view segment) {
	auto stem = std::string(segment.substr(0, segment.find('.')));
	std::transform(stem.begin(), stem.end(), stem.begin(), Lower);
	static constexpr auto kNames = std::array<std::string_view, 4>{
		"con", "prn", "aux", "nul",
	};
	if (std::find(kNames.begin(), kNames.end(), stem) != kNames.end()) {
		return true;
	}
	return stem.size() == 4
		&& (stem.starts_with("com") || stem.starts_with("lpt"))
		&& stem[3] >= '0'
		&& stem[3] <= '9';
}

[[nodiscard]] bool IsBadPathChar(char c) {
	const auto byte = static_cast<unsigned char>(c);
	return byte < 0x20
		|| byte == 0x7F
		|| std::string_view("\\:<>\"|?*").find(c) != std::string_view::npos;
}

[[nodiscard]] bool IsValidPort(std::string_view port) {
	if (port.empty() || port.size() > 5 || port.front() == '0') {
		return false;
	}
	auto value = 0;
	for (const auto c : port) {
		if (c < '0' || c > '9') {
			return false;
		}
		value = value * 10 + (c - '0');
	}
	return value <= 65535;
}

// "*", "host", "*.suffix", each but "*" with an optional ":port".
[[nodiscard]] bool IsValidNetworkEntry(std::string_view entry) {
	if (entry == "*") {
		return true;
	} else if (const auto colon = entry.rfind(':')
		; colon != std::string_view::npos) {
		if (!IsValidPort(entry.substr(colon + 1))) {
			return false;
		}
		entry = entry.substr(0, colon);
	}
	if (entry.starts_with("*.")) {
		entry.remove_prefix(2);
	}
	if (entry.empty() || entry.size() > kMaxHostLength) {
		return false;
	}
	auto label = std::size_t(0);
	for (const auto c : entry) {
		if (c == '.') {
			if (!label) {
				return false;
			}
			label = 0;
		} else if (IsLowerAlnum(c) || c == '-' || c == '_') {
			++label;
		} else {
			return false;
		}
	}
	return label != 0;
}

[[nodiscard]] bool IsValidSettingKey(std::string_view key) {
	return !key.empty()
		&& key.size() <= kMaxSettingKeyLength
		&& std::all_of(key.begin(), key.end(), [](char c) {
			return IsLowerAlnum(c)
				|| (c >= 'A' && c <= 'Z')
				|| c == '_'
				|| c == '.'
				|| c == '-';
		});
}

// "ru", "pt-br", "zh-hans": a lowercase letter, then [a-z0-9-].
[[nodiscard]] bool IsValidLocale(std::string_view locale) {
	return locale.size() >= 2
		&& locale.size() <= kMaxLocaleLength
		&& locale.front() >= 'a'
		&& locale.front() <= 'z'
		&& locale.back() != '-'
		&& std::all_of(locale.begin(), locale.end(), [](char c) {
			return IsLowerAlnum(c) || c == '-';
		});
}

// Readers return an error naming the field, or nullopt on success.
class Reader {
public:
	Reader(const Json &object, std::string prefix)
	: _object(object)
	, _prefix(std::move(prefix)) {
	}

	[[nodiscard]] std::string field(std::string_view key) const {
		return _prefix + std::string(key);
	}
	[[nodiscard]] const Json *find(std::string_view key) const {
		const auto i = _object.find(key);
		return (i == _object.end()) ? nullptr : &*i;
	}

	[[nodiscard]] std::optional<Error> string(
			std::string_view key,
			std::string &to,
			bool required) const {
		const auto value = find(key);
		if (!value) {
			if (required) {
				return Fail(field(key), "is required");
			}
			return std::nullopt;
		} else if (!value->is_string()) {
			return Fail(field(key), "must be a string");
		}
		to = value->get<std::string>();
		if (required && to.empty()) {
			return Fail(field(key), "must not be empty");
		}
		return std::nullopt;
	}

	[[nodiscard]] std::optional<Error> integer(
			std::string_view key,
			std::optional<std::int64_t> &to) const {
		const auto value = find(key);
		if (!value) {
			return std::nullopt;
		} else if (!value->is_number_integer()) {
			return Fail(field(key), "must be an integer");
		} else if (value->is_number_unsigned()
			&& value->get<std::uint64_t>()
				> std::uint64_t(std::numeric_limits<std::int64_t>::max())) {
			return Fail(field(key), "is out of range");
		}
		to = value->get<std::int64_t>();
		return std::nullopt;
	}

	[[nodiscard]] std::optional<Error> number(
			std::string_view key,
			std::optional<double> &to) const {
		const auto value = find(key);
		if (!value) {
			return std::nullopt;
		} else if (!value->is_number()
			|| !std::isfinite(value->get<double>())) {
			return Fail(field(key), "must be a number");
		}
		to = value->get<double>();
		return std::nullopt;
	}

	[[nodiscard]] std::optional<Error> strings(
			std::string_view key,
			std::vector<std::string> &to) const {
		const auto value = find(key);
		if (!value) {
			return std::nullopt;
		} else if (!value->is_array()) {
			return Fail(field(key), "must be an array of strings");
		}
		to.clear();
		for (auto i = std::size_t(); i != value->size(); ++i) {
			const auto &item = (*value)[i];
			if (!item.is_string()) {
				return Fail(Indexed(field(key), i), "must be a string");
			}
			to.push_back(item.get<std::string>());
		}
		return std::nullopt;
	}

private:
	const Json &_object;
	std::string _prefix;

};

[[nodiscard]] std::optional<Error> ParseIntSetting(
		const Reader &reader,
		SettingDescriptor &to) {
	auto value = std::optional<std::int64_t>();
	if (auto error = reader.integer("default", value)) {
		return error;
	} else if (auto error = reader.integer("min", to.min)) {
		return error;
	} else if (auto error = reader.integer("max", to.max)) {
		return error;
	} else if (to.min && to.max && *to.min > *to.max) {
		return Fail(reader.field("min"), "must not exceed max");
	}
	to.defaultInt = value.value_or(to.min.value_or(0));
	if ((to.min && to.defaultInt < *to.min)
		|| (to.max && to.defaultInt > *to.max)) {
		return Fail(reader.field("default"), "must be within min and max");
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Error> ParseNumberSetting(
		const Reader &reader,
		SettingDescriptor &to) {
	auto value = std::optional<double>();
	if (auto error = reader.number("default", value)) {
		return error;
	} else if (auto error = reader.number("min", to.minNumber)) {
		return error;
	} else if (auto error = reader.number("max", to.maxNumber)) {
		return error;
	} else if (auto error = reader.number("step", to.step)) {
		return error;
	} else if (to.minNumber && to.maxNumber && *to.minNumber > *to.maxNumber) {
		return Fail(reader.field("min"), "must not exceed max");
	} else if (to.step && *to.step <= 0.) {
		return Fail(reader.field("step"), "must be positive");
	}
	to.defaultNumber = value.value_or(to.minNumber.value_or(0.));
	if ((to.minNumber && to.defaultNumber < *to.minNumber)
		|| (to.maxNumber && to.defaultNumber > *to.maxNumber)) {
		return Fail(reader.field("default"), "must be within min and max");
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Error> ParseColorSetting(
		const Reader &reader,
		SettingDescriptor &to) {
	to.defaultString = "#000000";
	if (auto error = reader.string("default", to.defaultString, false)) {
		return error;
	} else if (!IsValidColor(to.defaultString)) {
		return Fail(
			reader.field("default"),
			"must be a #RRGGBB color, got " + Shown(to.defaultString));
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Error> ParseLocales(
		const Reader &reader,
		Locales &to) {
	const auto value = reader.find("locales");
	if (!value) {
		return std::nullopt;
	} else if (!value->is_object()) {
		return Fail(reader.field("locales"), "must be an object");
	}
	for (const auto &[locale, texts] : value->items()) {
		const auto prefix = reader.field("locales") + '.' + locale;
		if (!IsValidLocale(locale)) {
			return Fail(prefix, "must be a language code like \"ru\" or \"pt-br\"");
		} else if (!texts.is_object()) {
			return Fail(prefix, "must be an object");
		}
		const auto inner = Reader(texts, prefix + '.');
		auto &localized = to[locale];
		if (auto error = inner.string("name", localized.name, false)) {
			return error;
		} else if (auto error = inner.string(
				"description",
				localized.description,
				false)) {
			return error;
		}
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Error> ParseChoiceSetting(
		const Reader &reader,
		SettingDescriptor &to) {
	if (!reader.find("options")) {
		return Fail(reader.field("options"), "is required");
	} else if (auto error = reader.strings("options", to.options)) {
		return error;
	} else if (to.options.empty()) {
		return Fail(reader.field("options"), "must not be empty");
	}
	for (auto i = to.options.begin(); i != to.options.end(); ++i) {
		if (std::find(to.options.begin(), i, *i) != i) {
			return Fail(reader.field("options"), "duplicate " + Quoted(*i));
		}
	}
	if (!reader.find("default")) {
		to.defaultString = to.options.front();
		return std::nullopt;
	} else if (auto error = reader.string("default", to.defaultString, false)) {
		return error;
	}
	const auto known = std::find(
		to.options.begin(),
		to.options.end(),
		to.defaultString);
	if (known == to.options.end()) {
		return Fail(reader.field("default"), "must be one of options");
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Error> ParseSetting(
		const Json &value,
		const std::string &prefix,
		SettingDescriptor &to) {
	if (!value.is_object()) {
		return Fail(prefix, "must be an object");
	}
	const auto reader = Reader(value, prefix + '.');
	auto type = std::string();
	if (auto error = reader.string("key", to.key, true)) {
		return error;
	} else if (!IsValidSettingKey(to.key)) {
		return Fail(reader.field("key"), "must be 1-64 of [A-Za-z0-9_.-]");
	} else if (auto error = reader.string("title", to.title, true)) {
		return error;
	} else if (auto error = reader.string("type", type, true)) {
		return error;
	} else if (auto error = reader.string(
			"description",
			to.description,
			false)) {
		return error;
	} else if (auto error = reader.string(
			"placeholder",
			to.placeholder,
			false)) {
		return error;
	} else if (auto error = ParseLocales(reader, to.locales)) {
		return error;
	}
	if (type == "string") {
		to.type = SettingType::String;
		return reader.string("default", to.defaultString, false);
	} else if (type == "bool") {
		to.type = SettingType::Bool;
		const auto def = reader.find("default");
		if (def && !def->is_boolean()) {
			return Fail(reader.field("default"), "must be a boolean");
		}
		to.defaultBool = def && def->get<bool>();
		return std::nullopt;
	} else if (type == "int") {
		to.type = SettingType::Int;
		return ParseIntSetting(reader, to);
	} else if (type == "choice" || type == "select") {
		to.type = (type == "choice") ? SettingType::Choice : SettingType::Select;
		return ParseChoiceSetting(reader, to);
	} else if (type == "number") {
		to.type = SettingType::Number;
		return ParseNumberSetting(reader, to);
	} else if (type == "color") {
		to.type = SettingType::Color;
		return ParseColorSetting(reader, to);
	} else if (type == "secret") {
		to.type = SettingType::Secret;
		return reader.string("default", to.defaultString, false);
	}
	return Fail(reader.field("type"), "unknown type " + Quoted(type));
}

[[nodiscard]] std::optional<Error> ParseSettings(
		const Reader &reader,
		std::vector<SettingDescriptor> &to) {
	const auto value = reader.find("settings");
	if (!value) {
		return std::nullopt;
	} else if (!value->is_array()) {
		return Fail("settings", "must be an array");
	}
	for (auto i = std::size_t(); i != value->size(); ++i) {
		const auto prefix = Indexed("settings", i);
		auto setting = SettingDescriptor();
		if (auto error = ParseSetting((*value)[i], prefix, setting)) {
			return error;
		}
		for (const auto &existing : to) {
			if (existing.key == setting.key) {
				return Fail(prefix + ".key", "duplicate " + Quoted(setting.key));
			}
		}
		to.push_back(std::move(setting));
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Error> ParseRuntime(
		const Reader &reader,
		Runtime &to) {
	auto runtime = std::string("js");
	if (auto error = reader.string("runtime", runtime, false)) {
		return error;
	} else if (runtime == "js") {
		to = Runtime::Js;
		return std::nullopt;
	} else if (runtime == "python" || runtime == "python-native") {
		return Fail("runtime", Quoted(runtime) + " needs a newer LuxuryGram");
	}
	return Fail("runtime", "unknown runtime " + Quoted(runtime));
}

[[nodiscard]] std::optional<Error> ParseApi(const Reader &reader, int &to) {
	const auto api = reader.find("api");
	if (!api) {
		return Fail("api", "is required");
	} else if (!api->is_number_integer()) {
		return Fail("api", "must be an integer");
	}
	const auto newer = api->is_number_unsigned()
		? (api->get<std::uint64_t>() > std::uint64_t(kPluginApiVersion))
		: (api->get<std::int64_t>() > kPluginApiVersion);
	if (newer) {
		return Fail(
			"api",
			"plugin needs API " + api->dump()
				+ ", this LuxuryGram supports up to "
				+ std::to_string(kPluginApiVersion)
				+ "; update LuxuryGram");
	} else if (api->get<std::int64_t>() < 1) {
		return Fail("api", "must be at least 1");
	}
	to = api->get<int>();
	return std::nullopt;
}

[[nodiscard]] std::optional<Error> ParseFiles(
		const Reader &reader,
		Manifest &to) {
	if (auto error = reader.string("main", to.main, true)) {
		return error;
	} else if (!IsSafeRelativePath(to.main)) {
		return Fail("main", "must be a safe relative path");
	} else if (!reader.find("files")) {
		to.files = { to.main };
		return std::nullopt;
	} else if (auto error = reader.strings("files", to.files)) {
		return error;
	} else if (to.files.size() > std::size_t(kMaxPluginFiles)) {
		return Fail(
			"files",
			"more than " + std::to_string(kMaxPluginFiles) + " files");
	}
	const auto begin = to.files.begin();
	for (auto i = std::size_t(); i != to.files.size(); ++i) {
		const auto &file = to.files[i];
		if (!IsSafeRelativePath(file)) {
			return Fail(Indexed("files", i), "must be a safe relative path");
		} else if (std::find(begin, begin + i, file) != begin + i) {
			return Fail(Indexed("files", i), "duplicate " + Quoted(file));
		}
	}
	if (std::find(to.files.begin(), to.files.end(), to.main)
		== to.files.end()) {
		return Fail("files", "must include main " + Quoted(to.main));
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Error> ParsePermissions(
		const Reader &reader,
		Manifest &to) {
	auto ids = std::vector<std::string>();
	if (auto error = reader.strings("permissions", ids)) {
		return error;
	}
	for (auto i = std::size_t(); i != ids.size(); ++i) {
		const auto permission = PermissionFromId(ids[i]);
		if (!permission) {
			return Fail(
				Indexed("permissions", i),
				"unknown permission " + Quoted(ids[i]));
		}
		to.permissions.add(*permission);
	}
	if (auto error = reader.strings("network", to.network)) {
		return error;
	} else if (to.permissions.has(Permission::Network) && to.network.empty()) {
		return Fail("network", "is required with the network permission");
	}
	for (auto i = std::size_t(); i != to.network.size(); ++i) {
		auto &entry = to.network[i];
		std::transform(entry.begin(), entry.end(), entry.begin(), Lower);
		if (!IsValidNetworkEntry(entry)) {
			return Fail(Indexed("network", i), "must be a host, *.suffix or *, with an optional :port");
		}
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Error> ParseVersions(
		const Reader &reader,
		Manifest &to) {
	if (auto error = reader.string("version", to.version, true)) {
		return error;
	} else if (!ParseVersion(to.version)) {
		return Fail("version", "must look like 1.2.0");
	} else if (auto error = reader.string("minClient", to.minClient, false)) {
		return error;
	} else if (!to.minClient.empty() && !ParseVersion(to.minClient)) {
		return Fail("minClient", "must look like 1.2.0");
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Error> ParseTexts(
		const Reader &reader,
		Manifest &to) {
	if (auto error = reader.string("name", to.name, true)) {
		return error;
	} else if (auto error = reader.string("description", to.description, false)) {
		return error;
	} else if (auto error = reader.string("author", to.author, false)) {
		return error;
	} else if (auto error = reader.string("homepage", to.homepage, false)) {
		return error;
	} else if (!to.homepage.empty()
		&& !to.homepage.starts_with("https://")
		&& !to.homepage.starts_with("http://")) {
		return Fail("homepage", "must be an http(s) link");
	}
	return ParseLocales(reader, to.locales);
}

} // namespace

// The id is a directory name: Windows drops a trailing '.' (so "a.b."
// would open "a.b"), and device names open the device, not a folder.
bool IsValidPluginId(std::string_view id) {
	return id.size() >= 3
		&& id.size() <= 64
		&& IsLowerAlnum(id.front())
		&& IsLowerAlnum(id.back())
		&& std::all_of(id.begin() + 1, id.end(), [](char c) {
			return IsLowerAlnum(c) || c == '.' || c == '_' || c == '-';
		})
		&& (id.find("..") == std::string_view::npos)
		&& !IsWindowsDevice(id);
}

bool IsValidColor(std::string_view color) {
	const auto hex = [](char c) {
		return (c >= '0' && c <= '9')
			|| (c >= 'a' && c <= 'f')
			|| (c >= 'A' && c <= 'F');
	};
	return color.size() == 7
		&& color.front() == '#'
		&& std::all_of(color.begin() + 1, color.end(), hex);
}

bool IsSafeRelativePath(std::string_view path) {
	if (path.empty()) {
		return false;
	}
	while (true) {
		const auto slash = path.find('/');
		const auto segment = path.substr(0, slash);
		if (segment.empty()
			|| segment == "."
			|| segment == ".."
			|| segment.ends_with('.')
			|| segment.ends_with(' ')
			|| std::any_of(segment.begin(), segment.end(), IsBadPathChar)
			|| IsWindowsDevice(segment)) {
			return false;
		} else if (slash == std::string_view::npos) {
			return true;
		}
		path = path.substr(slash + 1);
	}
}

Result<Manifest> ParseManifest(std::string_view json) {
	if (json.size() > kMaxManifestBytes) {
		return Fail("plugin.json", "is larger than 256 KiB");
	}
	const auto document = Json::parse(json, nullptr, false);
	if (document.is_discarded()) {
		return Fail("plugin.json", "is not valid JSON");
	} else if (!document.is_object()) {
		return Fail("plugin.json", "must be an object");
	}
	const auto reader = Reader(document, std::string());
	auto result = Manifest();
	auto error = reader.string("id", result.id, true);
	if (!error && !IsValidPluginId(result.id)) {
		error = Fail(
			"id",
			"must match ^[a-z0-9][a-z0-9._-]{1,62}[a-z0-9]$"
				" without \"..\" or a device name, got "
				+ Shown(result.id));
	}
	// API and runtime first: a newer plugin gets "update LuxuryGram"
	// instead of an error about a field this client does not know yet.
	if (!error) {
		error = ParseApi(reader, result.api);
	}
	if (!error) {
		error = ParseRuntime(reader, result.runtime);
	}
	if (!error) {
		error = ParseTexts(reader, result);
	}
	if (!error) {
		error = ParseVersions(reader, result);
	}
	if (!error) {
		error = ParseFiles(reader, result);
	}
	if (!error) {
		error = ParsePermissions(reader, result);
	}
	if (!error) {
		error = ParseSettings(reader, result.settings);
	}
	if (error) {
		return *error;
	}
	return result;
}

} // namespace Luxury::Plugins
