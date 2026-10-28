// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

// Qt-free decisions of the plugin UI extension points (plugin_host_ext_ui.cpp,
// plugin_hooks_ui.cpp), compiled by tools/tests/plugin_ui_ext_regressions.py.

#include "luxury/libs/json.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Luxury::Plugins {

// Chat menu items, header buttons and compose menu items, each list.
inline constexpr auto kMaxUiItems = std::size_t(100);
// Messages one plugin keeps a label for.
inline constexpr auto kMaxDecorations = std::size_t(1000);
inline constexpr auto kMaxFormFields = std::size_t(20);
inline constexpr auto kMaxFormText = std::size_t(256);
inline constexpr auto kMaxSelectOptions = std::size_t(50);
inline constexpr auto kMaxDecorationChars = 32;
inline constexpr auto kMaxPluginUrl = std::size_t(2048);

// Routed to Instance::uiExt before the generic "ui." branch.
[[nodiscard]] inline bool IsUiExtFunction(std::string_view function) {
	constexpr auto kNames = std::array<std::string_view, 9>{
		"ui.addChatMenuItem",
		"ui.addHeaderButton",
		"ui.addComposeMenuItem",
		"ui.showForm",
		"ui.currentChat",
		"ui.openChat",
		"ui.openUrl",
		"ui.decorateMessage",
		"ui.refreshMessage",
	};
	for (const auto &name : kNames) {
		if (name == function) {
			return true;
		}
	}
	return false;
}

// One row of ui.showForm. value is the default: a string for text and
// select, a bool for toggle.
struct FormField {
	std::string type;
	std::string key;
	std::string label;
	std::string placeholder;
	nlohmann::json value;
	std::vector<std::string> options;
};

[[nodiscard]] inline bool IsFormKey(std::string_view key) {
	if (key.empty() || key.size() > 32) {
		return false;
	}
	for (const auto c : key) {
		const auto ok = (c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9')
			|| (c == '_');
		if (!ok) {
			return false;
		}
	}
	return true;
}

// {title?, fields: [{type, key, label?, default?, placeholder?, options?}]}.
// Strict: an unknown member or field type is an error, so a typo never
// shows a form that silently drops a field. error names the bad path.
[[nodiscard]] inline std::optional<std::vector<FormField>> ParseFormSchema(
		const nlohmann::json &schema,
		std::string &error) {
	const auto fail = [&](std::string text) {
		error = std::move(text);
		return std::nullopt;
	};
	const auto shortString = [](const nlohmann::json &value) {
		return value.is_string()
			&& value.get_ref<const std::string&>().size() <= kMaxFormText;
	};
	if (!schema.is_object()) {
		return fail("the schema must be an object");
	}
	for (const auto &[name, value] : schema.items()) {
		if (name == "title") {
			if (!shortString(value)) {
				return fail("title must be a string of at most 256 bytes");
			}
		} else if (name != "fields") {
			return fail("unknown schema member " + name);
		}
	}
	const auto fields = schema.find("fields");
	if (fields == schema.end()
		|| !fields->is_array()
		|| fields->empty()
		|| fields->size() > kMaxFormFields) {
		return fail("fields must be an array of 1 to 20 fields");
	}
	auto result = std::vector<FormField>();
	for (std::size_t i = 0; i != fields->size(); ++i) {
		const auto &entry = (*fields)[i];
		const auto where = "fields[" + std::to_string(i) + "]";
		if (!entry.is_object()) {
			return fail(where + " must be an object");
		}
		auto field = FormField();
		const auto type = entry.find("type");
		if (type == entry.end() || !type->is_string()) {
			return fail(where + ".type must be a string");
		}
		field.type = type->get<std::string>();
		if (field.type != "text"
			&& field.type != "toggle"
			&& field.type != "select") {
			return fail(where + ".type: unknown field type " + field.type);
		}
		for (const auto &[name, value] : entry.items()) {
			if (name == "type") {
				continue;
			} else if (name == "key") {
				if (!value.is_string()
					|| !IsFormKey(value.get_ref<const std::string&>())) {
					return fail(where + ".key must be [A-Za-z0-9_]{1,32}");
				}
				field.key = value.get<std::string>();
			} else if (name == "label" || name == "placeholder") {
				if (!shortString(value)) {
					return fail(where + "." + name
						+ " must be a string of at most 256 bytes");
				}
				(name == "label" ? field.label : field.placeholder)
					= value.get<std::string>();
			} else if (name == "default") {
				field.value = value;
			} else if (name == "options" && field.type == "select") {
				if (!value.is_array()
					|| value.empty()
					|| value.size() > kMaxSelectOptions) {
					return fail(where + ".options must be 1 to 50 strings");
				}
				for (const auto &option : value) {
					if (!shortString(option)) {
						return fail(where + ".options must be 1 to 50 strings");
					}
					field.options.push_back(option.get<std::string>());
				}
			} else {
				return fail(where + ": unknown member " + name);
			}
		}
		if (field.key.empty()) {
			return fail(where + ".key is missing");
		}
		for (const auto &other : result) {
			if (other.key == field.key) {
				return fail(where + ".key repeats " + field.key);
			}
		}
		if (field.type == "toggle") {
			if (field.value.is_null()) {
				field.value = false;
			} else if (!field.value.is_boolean()) {
				return fail(where + ".default must be a boolean");
			}
		} else if (field.value.is_null()) {
			field.value = (field.type == "select" && !field.options.empty())
				? field.options.front()
				: std::string();
		} else if (!shortString(field.value)) {
			return fail(where + ".default must be a string");
		}
		if (field.type == "select") {
			if (field.options.empty()) {
				return fail(where + ".options is missing");
			}
			const auto &value = field.value.get_ref<const std::string&>();
			auto known = false;
			for (const auto &option : field.options) {
				known = known || (option == value);
			}
			if (!known) {
				return fail(where + ".default is not one of the options");
			}
		}
		result.push_back(std::move(field));
	}
	return result;
}

// ui.openUrl: a web page only. tg:, file: and the rest of the schemes are
// refused here, the link still goes through the client's confirmation.
[[nodiscard]] inline bool IsPluginOpenableUrl(std::string_view url) {
	if (url.size() > kMaxPluginUrl) {
		return false;
	}
	for (const auto c : url) {
		const auto code = static_cast<unsigned char>(c);
		if (code <= 0x20 || code == 0x7f) {
			return false;
		}
	}
	const auto lower = [](char c) {
		return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
	};
	const auto starts = [&](std::string_view prefix) {
		if (url.size() < prefix.size()) {
			return false;
		}
		for (std::size_t i = 0; i != prefix.size(); ++i) {
			if (lower(url[i]) != prefix[i]) {
				return false;
			}
		}
		return true;
	};
	const auto scheme = starts("https://")
		? std::size_t(8)
		: starts("http://")
		? std::size_t(7)
		: std::size_t(0);
	if (!scheme) {
		return false;
	}
	const auto rest = url.substr(scheme);
	const auto host = rest.substr(0, rest.find_first_of("/?#"));
	return !host.empty() && host.front() != '@' && host.front() != ':';
}

// The label a plugin puts on a message: one line, trimmed, at most
// kMaxDecorationChars code points. Empty removes the label.
[[nodiscard]] inline std::string DecorationText(std::string text) {
	for (auto &c : text) {
		if (c == '\n' || c == '\r' || c == '\t') {
			c = ' ';
		}
	}
	const auto first = text.find_first_not_of(' ');
	if (first == std::string::npos) {
		return std::string();
	}
	text = text.substr(first, text.find_last_not_of(' ') - first + 1);
	auto chars = 0;
	for (std::size_t i = 0; i != text.size(); ++i) {
		const auto lead = (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80;
		if (lead && ++chars > kMaxDecorationChars) {
			text.resize(i);
			break;
		}
	}
	return text;
}

} // namespace Luxury::Plugins
