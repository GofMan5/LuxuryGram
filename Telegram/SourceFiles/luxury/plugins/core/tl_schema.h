/*
This file is part of LuxuryGram, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Luxury::Plugins::Tl {

template <typename T>
struct Result {
	std::optional<T> value;
	std::string error;

	explicit operator bool() const {
		return value.has_value();
	}
	static Result Fail(std::string message) {
		return { std::nullopt, std::move(message) };
	}
};

struct TypeRef {
	enum class Kind {
		Int,
		Long,
		Double,
		String,
		Bytes,
		Int128,
		Int256,
		Bool,
		True,
		Flags,
		Vector, // args[0] is the element; bare is true for lowercase vector.
		Boxed, // name is the boxed type, empty for any object.
		Bare, // name is a constructor, or a type when written as %Type.
		Generic, // name is the {X:Type} parameter used as a result.
		Query, // !X: a whole boxed method call.
	};
	Kind kind = Kind::Boxed;
	bool bare = false;
	std::string name;
	std::vector<TypeRef> args;
};

struct Param {
	std::string name;
	TypeRef type;
	int flagParam = -1; // Index of the # param this one depends on.
	int flagBit = -1;
};

struct Combinator {
	std::string name;
	uint32_t id = 0;
	bool function = false;
	std::string generic; // X of {X:Type}, empty when not generic.
	std::vector<Param> params;
	std::string resultType;
};

// A parameter or result type; X names the {X:Type} parameter, if any.
[[nodiscard]] Result<TypeRef> ParseType(
	std::string_view text,
	std::string_view generic = {});

class Schema {
public:
	[[nodiscard]] static Result<Schema> Parse(std::string_view text);

	// tdesktop's generate_tl.py normalization: CRC32 of the line without
	// the id, `?true` params, `<>` and `{}`, with bytes spelled string.
	[[nodiscard]] static uint32_t ComputeId(std::string_view line);

	[[nodiscard]] const Combinator *constructor(std::string_view name) const;
	[[nodiscard]] const Combinator *constructor(uint32_t id) const;
	[[nodiscard]] const Combinator *method(std::string_view name) const;
	[[nodiscard]] const Combinator *method(uint32_t id) const;
	// A lowercase constructor name, or a %Type with one constructor.
	[[nodiscard]] const Combinator *bareConstructor(std::string_view name) const;
	[[nodiscard]] std::vector<const Combinator*> constructorsOf(
		std::string_view type) const;
	[[nodiscard]] std::optional<std::string> MethodReturnType(
		std::string_view name) const;

	[[nodiscard]] const std::vector<Combinator> &combinators() const {
		return _combinators;
	}
	[[nodiscard]] size_t typesCount() const {
		return _types.size();
	}

private:
	[[nodiscard]] std::string add(Combinator &&combinator);

	std::vector<Combinator> _combinators;
	std::unordered_map<std::string, size_t> _constructorsByName;
	std::unordered_map<uint32_t, size_t> _constructorsById;
	std::unordered_map<std::string, size_t> _methodsByName;
	std::unordered_map<uint32_t, size_t> _methodsById;
	std::unordered_map<std::string, std::vector<size_t>> _types;

};

} // namespace Luxury::Plugins::Tl
