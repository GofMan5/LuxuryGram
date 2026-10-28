// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

// Qt-free part of the data API (lux.chats, lux.messages, lux.account):
// permissions, entity conversion and the 777000 filter, compiled by
// tools/tests/plugin_data_regressions.py without the client.

#include "luxury/plugins/plugin_host_logic.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Luxury::Plugins {

// messages.downloadMedia and messages.sendFile move whole files through
// the JSON bridge as base64, so a file is capped well below the engine
// memory limit, and at the client's in-memory download limit
// (Storage::kMaxFileInMemory, asserted in plugin_host_ext_data.cpp).
inline constexpr auto kMaxMediaBytes = std::int64_t(10 * 1024 * 1024);
inline constexpr auto kMaxHistoryLimit = 100;
inline constexpr auto kMaxChatsLimit = 500;
inline constexpr auto kMaxEntities = 1000;

// The permission a data function needs, nullopt for any other name.
[[nodiscard]] inline std::optional<Permission> DataPermission(
		std::string_view function) {
	if (function == "chats.get"
		|| function == "chats.list"
		|| function == "messages.get"
		|| function == "messages.history"
		|| function == "messages.downloadMedia"
		|| function == "account.me") {
		return Permission::MessagesRead;
	} else if (function == "messages.edit"
		|| function == "messages.delete"
		|| function == "messages.forward"
		|| function == "messages.markRead") {
		return Permission::MessagesManage;
	} else if (function == "messages.sendFile") {
		return Permission::MessagesSend;
	} else if (function == "ui.notify") {
		return Permission::Ui;
	}
	return std::nullopt;
}

// The names Instance::data() answers; settings.set needs no permission,
// like settings.get: a plugin only writes its own declared settings.
[[nodiscard]] inline bool IsDataFunction(std::string_view function) {
	return DataPermission(function).has_value()
		|| (function == "settings.set");
}

[[nodiscard]] inline std::optional<Permission> DataEventPermission(
		std::string_view event) {
	if (event == "messageEdited"
		|| event == "messageDeleted"
		|| event == "chatRead"
		|| event == "accountChanged") {
		return Permission::MessagesRead;
	}
	return std::nullopt;
}

// A chat a plugin may see: a real peer, never the service chat.
[[nodiscard]] inline bool ChatVisible(std::int64_t dialogId) {
	return IsBareDialogId(dialogId) && (dialogId != kServiceUserId);
}

// Indexed by EntityType of ui/text/text_entity.h, checked there by a
// static_assert in plugin_hooks.cpp; "" is EntityType::Invalid.
inline constexpr std::string_view kEntityNames[] = {
	"",
	"url",
	"textUrl",
	"email",
	"hashtag",
	"cashtag",
	"mention",
	"mentionName",
	"customEmoji",
	"botCommand",
	"mediaTimestamp",
	"colorized",
	"phone",
	"bankCard",
	"tonAddress",
	"bold",
	"semibold",
	"italic",
	"underline",
	"strikethrough",
	"code",
	"pre",
	"blockquote",
	"spoiler",
	"subscript",
	"superscript",
	"marked",
	"formattedDate",
};
inline constexpr auto kEntityTextUrl = 2;
inline constexpr auto kEntityMentionName = 7;
inline constexpr auto kEntityCustomEmoji = 8;
inline constexpr auto kEntityPre = 21;

// The field that carries an entity's data, nullptr when it has none.
[[nodiscard]] inline const char *EntityDataKey(int type) {
	switch (type) {
	case kEntityTextUrl: return "url";
	case kEntityMentionName: return "userId";
	case kEntityCustomEmoji: return "emojiId";
	case kEntityPre: return "language";
	}
	return nullptr;
}

[[nodiscard]] inline int EntityTypeByName(std::string_view name) {
	const auto count = int(std::size(kEntityNames));
	for (auto i = 1; i != count; ++i) {
		if (kEntityNames[i] == name) {
			return i;
		}
	}
	return 0;
}

// One entity in UTF-16 units, as EntityInText counts them. For
// mentionName the data is the bare user id.
struct EntityDraft {
	int type = 0;
	int offset = 0;
	int length = 0;
	std::string data = std::string();
};

// UTF-16 length of UTF-8 text: what QString::size() says for it.
[[nodiscard]] inline int Utf16Length(std::string_view text) {
	auto result = 0;
	for (const auto c : text) {
		const auto byte = static_cast<unsigned char>(c);
		if ((byte & 0xC0) != 0x80) {
			result += ((byte & 0xF8) == 0xF0) ? 2 : 1;
		}
	}
	return result;
}

[[nodiscard]] inline bool EntityValid(
		const EntityDraft &entity,
		int textLength) {
	if (entity.type <= 0
		|| entity.type >= int(std::size(kEntityNames))
		|| entity.offset < 0
		|| entity.length <= 0
		|| entity.offset > textLength
		|| entity.length > textLength - entity.offset) {
		return false;
	} else if (entity.type == kEntityMentionName) {
		return !entity.data.empty()
			&& !IsServiceUserId(nlohmann::json(entity.data));
	} else if (entity.type == kEntityTextUrl
		|| entity.type == kEntityCustomEmoji) {
		return !entity.data.empty();
	}
	return true;
}

// The entities a plugin returned: unknown, malformed and out-of-range
// entries are dropped, as is a mention of 777000. Sorted by offset, as
// ConvertEntitiesToTextTags reads them.
[[nodiscard]] inline std::vector<EntityDraft> ParseEntities(
		const nlohmann::json &list,
		int textLength) {
	auto result = std::vector<EntityDraft>();
	if (!list.is_array()) {
		return result;
	}
	for (const auto &item : list) {
		if (int(result.size()) == kMaxEntities) {
			break;
		} else if (!item.is_object()) {
			continue;
		}
		const auto type = item.find("type");
		const auto offset = item.find("offset");
		const auto length = item.find("length");
		if (type == item.end()
			|| !type->is_string()
			|| offset == item.end()
			|| !offset->is_number_integer()
			|| length == item.end()
			|| !length->is_number_integer()) {
			continue;
		}
		const auto from = offset->get<std::int64_t>();
		const auto size = length->get<std::int64_t>();
		if (from < 0 || from > textLength || size <= 0 || size > textLength) {
			continue;
		}
		auto entity = EntityDraft{
			.type = EntityTypeByName(type->get<std::string>()),
			.offset = int(from),
			.length = int(size),
		};
		if (const auto key = EntityDataKey(entity.type)) {
			const auto data = item.find(key);
			if (data != item.end() && data->is_string()) {
				entity.data = data->get<std::string>();
			} else if (data != item.end() && data->is_number_integer()) {
				entity.data = data->dump();
			}
		}
		if (EntityValid(entity, textLength)) {
			result.push_back(std::move(entity));
		}
	}
	std::stable_sort(result.begin(), result.end(), [](
			const EntityDraft &a,
			const EntityDraft &b) {
		return a.offset < b.offset;
	});
	return result;
}

// null for an entity a plugin must not see: a mention of 777000.
[[nodiscard]] inline nlohmann::json EntityJson(const EntityDraft &entity) {
	if (entity.type <= 0
		|| entity.type >= int(std::size(kEntityNames))
		|| (entity.type == kEntityMentionName
			&& (entity.data.empty()
				|| IsServiceUserId(nlohmann::json(entity.data))))) {
		return nullptr;
	}
	auto result = nlohmann::json{
		{ "type", std::string(kEntityNames[entity.type]) },
		{ "offset", entity.offset },
		{ "length", entity.length },
	};
	if (const auto key = EntityDataKey(entity.type)) {
		result[key] = entity.data;
	}
	return result;
}

// The id a payload names a message by: a decimal string, as MessageJson
// writes it, or a plain number.
[[nodiscard]] inline std::optional<std::int64_t> MessageIdArg(
		const nlohmann::json &value) {
	auto result = std::int64_t();
	if (value.is_number_integer()) {
		result = value.get<std::int64_t>();
	} else if (value.is_string()) {
		const auto &text = value.get_ref<const std::string&>();
		const auto end = text.data() + text.size();
		const auto parsed = std::from_chars(text.data(), end, result);
		if (parsed.ec != std::errc() || parsed.ptr != end) {
			return std::nullopt;
		}
	} else {
		return std::nullopt;
	}
	return (result > 0) ? std::make_optional(result) : std::nullopt;
}

// messages.delete and messages.forward take one id or an array of ids;
// nullopt if any of them is not a message id.
[[nodiscard]] inline std::optional<std::vector<std::int64_t>> MessageIdsArg(
		const nlohmann::json &value) {
	auto result = std::vector<std::int64_t>();
	if (!value.is_array()) {
		if (const auto id = MessageIdArg(value)) {
			result.push_back(*id);
			return result;
		}
		return std::nullopt;
	} else if (value.empty() || value.size() > std::size_t(kMaxHistoryLimit)) {
		return std::nullopt;
	}
	for (const auto &item : value) {
		const auto id = MessageIdArg(item);
		if (!id) {
			return std::nullopt;
		}
		result.push_back(*id);
	}
	return result;
}

// The limit argument of chats.list and messages.history.
[[nodiscard]] inline int LimitArg(
		const nlohmann::json &value,
		int fallback,
		int maximum) {
	if (!value.is_number_integer()) {
		return fallback;
	}
	const auto limit = value.get<std::int64_t>();
	return (limit < 1) ? 1 : (limit > maximum) ? maximum : int(limit);
}

} // namespace Luxury::Plugins
