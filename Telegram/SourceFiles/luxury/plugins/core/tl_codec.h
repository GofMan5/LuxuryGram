/*
This file is part of LuxuryGram, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "luxury/plugins/core/tl_schema.h"
#include "luxury/libs/json.hpp"

#include <functional>

namespace Luxury::Plugins::Tl {

// Unpacks gzip_packed#3072cfa1 data; std::nullopt means corrupt input.
using Inflate = std::function<std::optional<std::string>(std::string_view)>;

inline constexpr auto kMaxDepth = 64;

// JSON form per docs/plugins/ARCHITECTURE.md: long is a decimal string,
// bytes base64, int128/int256 hex in wire byte order, `_` the constructor.
// Errors name the JSON path, e.g. `peer._: unknown constructor "x"`.

// {"_": "messages.sendMessage", ...} -> the boxed request, exactly as
// tdesktop's generated serializer writes it.
[[nodiscard]] Result<std::vector<uint32_t>> EncodeMethod(
	const Schema &schema,
	const nlohmann::json &call);

// One value of a TL type, e.g. "InputPeer", "Vector<long>" or "int128".
[[nodiscard]] Result<std::vector<uint32_t>> EncodeValue(
	const Schema &schema,
	std::string_view type,
	const nlohmann::json &value);

// The type a call returns, following !X through invokeWith* wrappers.
[[nodiscard]] std::optional<std::string> CallReturnType(
	const Schema &schema,
	const nlohmann::json &call);

// returnType is a method's result type, e.g. "Updates" or "Vector<long>".
// The whole buffer must be consumed.
[[nodiscard]] Result<nlohmann::json> DecodeResult(
	const Schema &schema,
	std::string_view returnType,
	const uint32_t *data,
	size_t words,
	const Inflate &inflate = {});

// Any boxed object, e.g. an Updates received from the server.
[[nodiscard]] Result<nlohmann::json> DecodeObject(
	const Schema &schema,
	const uint32_t *data,
	size_t words,
	const Inflate &inflate = {});

} // namespace Luxury::Plugins::Tl
