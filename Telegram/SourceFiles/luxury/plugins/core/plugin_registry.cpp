// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/core/plugin_registry.h"

#include "luxury/libs/json.hpp"
#include "luxury/plugins/core/plugin_manifest.h"

#include <algorithm>

namespace Luxury::Plugins {
namespace {

using Json = nlohmann::json;

[[nodiscard]] std::string String(const Json &object, const char *key) {
	const auto i = object.find(key);
	return (i != object.end() && i->is_string())
		? i->get<std::string>()
		: std::string();
}

[[nodiscard]] std::vector<std::string> Strings(
		const Json &object,
		const char *key) {
	auto result = std::vector<std::string>();
	const auto i = object.find(key);
	if (i != object.end() && i->is_array()) {
		for (const auto &item : *i) {
			if (item.is_string()) {
				result.push_back(item.get<std::string>());
			}
		}
	}
	return result;
}

// Unknown ids are dropped: a newer client may know more permissions, and
// an id this client cannot enforce must never count as granted.
[[nodiscard]] PermissionSet Permissions(const Json &object, const char *key) {
	auto result = PermissionSet();
	for (const auto &id : Strings(object, key)) {
		if (const auto permission = PermissionFromId(id)) {
			result.add(*permission);
		}
	}
	return result;
}

[[nodiscard]] bool IsValidSource(const PluginSource &source) {
	return (source.owner.empty() || IsValidGithubName(source.owner))
		&& (source.repo.empty() || IsValidGithubName(source.repo))
		&& (source.subdir.empty() || IsSafeRelativePath(source.subdir))
		&& (source.ref.empty() || IsValidGitRef(source.ref))
		&& (source.label.empty() || IsValidGitRef(source.label));
}

[[nodiscard]] PluginSource ParseSource(const Json &object) {
	auto result = PluginSource();
	const auto i = object.find("source");
	if (i == object.end() || !i->is_object()) {
		return result;
	}
	result.owner = String(*i, "owner");
	result.repo = String(*i, "repo");
	result.subdir = String(*i, "subdir");
	result.channel = (String(*i, "channel") == "branch")
		? Channel::Branch
		: Channel::Release;
	result.ref = String(*i, "ref");
	result.label = String(*i, "label");
	return result;
}

[[nodiscard]] Json SerializeSource(const PluginSource &source) {
	return {
		{ "owner", source.owner },
		{ "repo", source.repo },
		{ "subdir", source.subdir },
		{ "channel", source.channel == Channel::Branch ? "branch" : "release" },
		{ "ref", source.ref },
		{ "label", source.label },
	};
}

} // namespace

Result<Registry> ParseRegistry(std::string_view json) {
	const auto document = Json::parse(json, nullptr, false);
	if (document.is_discarded() || !document.is_object()) {
		return Error{ "registry.json: is not a JSON object" };
	}
	auto result = Registry();
	const auto plugins = document.find("plugins");
	if (plugins == document.end()) {
		return result;
	} else if (!plugins->is_array()) {
		return Error{ "registry.json: plugins must be an array" };
	}
	for (auto i = std::size_t(); i != plugins->size(); ++i) {
		const auto &entry = (*plugins)[i];
		const auto where = "plugins[" + std::to_string(i) + "]: ";
		if (!entry.is_object()) {
			result.skipped.push_back(where + "not an object");
			continue;
		}
		auto plugin = InstalledPlugin();
		plugin.id = String(entry, "id");
		if (!IsValidPluginId(plugin.id)) {
			result.skipped.push_back(where + "bad id");
			continue;
		}
		const auto duplicate = std::any_of(
			result.plugins.begin(),
			result.plugins.end(),
			[&](const InstalledPlugin &other) { return other.id == plugin.id; });
		if (duplicate) {
			result.skipped.push_back(where + "duplicate id " + plugin.id);
			continue;
		}
		plugin.source = ParseSource(entry);
		if (!IsValidSource(plugin.source)) {
			result.skipped.push_back(where + "bad source");
			continue;
		}
		plugin.name = String(entry, "name");
		plugin.version = String(entry, "version");
		plugin.description = String(entry, "description");
		plugin.author = String(entry, "author");
		plugin.homepage = String(entry, "homepage");
		plugin.requested = Permissions(entry, "requested");
		plugin.network = Strings(entry, "network");
		plugin.granted = Permissions(entry, "granted");
		plugin.lastError = String(entry, "lastError");
		const auto enabled = entry.find("enabled");
		plugin.enabled = enabled != entry.end()
			&& enabled->is_boolean()
			&& enabled->get<bool>();
		const auto installedAt = entry.find("installedAt");
		if (installedAt != entry.end() && installedAt->is_number_integer()) {
			plugin.installedAt = installedAt->get<std::int64_t>();
		}
		result.plugins.push_back(std::move(plugin));
	}
	return result;
}

std::string SerializeRegistry(const Registry &registry) {
	auto plugins = Json::array();
	for (const auto &plugin : registry.plugins) {
		if (!IsValidPluginId(plugin.id)) {
			continue;
		}
		plugins.push_back({
			{ "id", plugin.id },
			{ "name", plugin.name },
			{ "version", plugin.version },
			{ "description", plugin.description },
			{ "author", plugin.author },
			{ "homepage", plugin.homepage },
			{ "requested", plugin.requested.ids() },
			{ "network", plugin.network },
			{ "source", SerializeSource(plugin.source) },
			{ "granted", plugin.granted.ids() },
			{ "enabled", plugin.enabled },
			{ "installedAt", plugin.installedAt },
			{ "lastError", plugin.lastError },
		});
	}
	const auto document = Json{
		{ "format", kRegistryFormat },
		{ "plugins", std::move(plugins) },
	};
	// Replace keeps a damaged UTF-8 lastError from throwing.
	return document.dump(1, '\t', false, Json::error_handler_t::replace);
}

bool NeedsNewConsent(PermissionSet granted, PermissionSet requested) {
	return !granted.contains(requested);
}

} // namespace Luxury::Plugins
