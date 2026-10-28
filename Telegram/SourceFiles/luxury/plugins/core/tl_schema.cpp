/*
This file is part of LuxuryGram, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "luxury/plugins/core/tl_schema.h"

#include <array>
#include <charconv>
#include <regex>

namespace Luxury::Plugins::Tl {
namespace {

struct Failure {
	std::string text;
};

[[noreturn]] void Fail(std::string text) {
	throw Failure{ std::move(text) };
}

std::string_view Trim(std::string_view text) {
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos) {
		return {};
	}
	const auto last = text.find_last_not_of(" \t\r\n");
	return text.substr(first, last - first + 1);
}

std::vector<std::string_view> Tokens(std::string_view text) {
	auto result = std::vector<std::string_view>();
	auto i = size_t(0);
	while (true) {
		i = text.find_first_not_of(" \t\r\n", i);
		if (i == std::string_view::npos) {
			return result;
		}
		const auto end = std::min(text.find_first_of(" \t\r\n", i), text.size());
		result.push_back(text.substr(i, end - i));
		i = end;
	}
}

bool IsLowerName(std::string_view name) {
	const auto dot = name.rfind('.');
	const auto first = name[(dot == std::string_view::npos) ? 0 : dot + 1];
	return (first >= 'a' && first <= 'z');
}

uint32_t Crc32(std::string_view data) {
	static const auto table = [] {
		auto result = std::array<uint32_t, 256>();
		for (auto i = uint32_t(0); i != 256; ++i) {
			auto value = i;
			for (auto bit = 0; bit != 8; ++bit) {
				value = (value & 1) ? (0xEDB88320U ^ (value >> 1)) : (value >> 1);
			}
			result[i] = value;
		}
		return result;
	}();
	auto crc = 0xFFFFFFFFU;
	for (const auto ch : data) {
		crc = table[(crc ^ static_cast<unsigned char>(ch)) & 0xFFU] ^ (crc >> 8);
	}
	return crc ^ 0xFFFFFFFFU;
}

TypeRef ParseTypeOrFail(std::string_view text, std::string_view generic) {
	using Kind = TypeRef::Kind;
	if (text.empty()) {
		Fail("empty type");
	} else if (text[0] == '!') {
		if (generic.empty() || text.substr(1) != generic) {
			Fail("bad query type \"" + std::string(text) + "\"");
		}
		return { Kind::Query, false, std::string(generic), {} };
	} else if (text[0] == '%') {
		auto result = ParseTypeOrFail(text.substr(1), generic);
		if (result.kind == Kind::Boxed) {
			result.kind = Kind::Bare;
		} else if (result.kind != Kind::Vector) {
			Fail("bad bare type \"" + std::string(text) + "\"");
		}
		result.bare = true;
		return result;
	} else if (text.starts_with("Vector<") || text.starts_with("vector<")) {
		if (!text.ends_with('>')) {
			Fail("bad vector type \"" + std::string(text) + "\"");
		}
		const auto inner = text.substr(7, text.size() - 8);
		return {
			Kind::Vector,
			(text[0] == 'v'),
			{},
			{ ParseTypeOrFail(inner, generic) },
		};
	}
	static const auto builtin = std::array<std::pair<std::string_view, Kind>, 10>{ {
		{ "int", Kind::Int },
		{ "long", Kind::Long },
		{ "double", Kind::Double },
		{ "string", Kind::String },
		{ "bytes", Kind::Bytes },
		{ "int128", Kind::Int128 },
		{ "int256", Kind::Int256 },
		{ "Bool", Kind::Bool },
		{ "true", Kind::True },
		{ "#", Kind::Flags },
	} };
	for (const auto &[name, kind] : builtin) {
		if (text == name) {
			return { kind, false, {}, {} };
		}
	}
	for (const auto ch : text) {
		const auto ok = (ch >= 'a' && ch <= 'z')
			|| (ch >= 'A' && ch <= 'Z')
			|| (ch >= '0' && ch <= '9')
			|| ch == '_'
			|| ch == '.';
		if (!ok) {
			Fail("bad type \"" + std::string(text) + "\"");
		}
	}
	if (text == generic) {
		return { Kind::Generic, false, std::string(text), {} };
	}
	const auto lower = IsLowerName(text);
	return { lower ? Kind::Bare : Kind::Boxed, lower, std::string(text), {} };
}

Combinator ParseCombinator(std::string_view statement, bool function) {
	const auto equals = statement.find('=');
	if (equals == std::string_view::npos) {
		Fail("missing '='");
	}
	const auto tokens = Tokens(statement.substr(0, equals));
	if (tokens.empty()) {
		Fail("missing combinator name");
	}
	auto result = Combinator();
	result.function = function;
	const auto head = tokens[0];
	const auto hash = head.find('#');
	result.name = std::string(head.substr(0, hash));
	if (result.name.empty()) {
		Fail("missing combinator name");
	}
	if (hash == std::string_view::npos) {
		result.id = Schema::ComputeId(statement);
	} else {
		const auto hex = head.substr(hash + 1);
		const auto end = hex.data() + hex.size();
		const auto parsed = std::from_chars(hex.data(), end, result.id, 16);
		if (hex.empty() || hex.size() > 8 || parsed.ptr != end) {
			Fail("bad id in \"" + std::string(head) + "\"");
		}
	}
	for (auto i = size_t(1); i != tokens.size(); ++i) {
		const auto token = tokens[i];
		if (token.starts_with('{')) {
			const auto colon = token.find(':');
			if (!token.ends_with(":Type}")
				|| colon != token.size() - 6
				|| !result.generic.empty()) {
				Fail("bad generic \"" + std::string(token) + "\"");
			}
			result.generic = std::string(token.substr(1, colon - 1));
			continue;
		}
		const auto colon = token.find(':');
		if (colon == std::string_view::npos || colon == 0) {
			Fail("bad param \"" + std::string(token) + "\"");
		}
		auto param = Param();
		param.name = std::string(token.substr(0, colon));
		auto type = token.substr(colon + 1);
		if (const auto question = type.find('?'); question != type.npos) {
			const auto condition = type.substr(0, question);
			const auto dot = condition.find('.');
			const auto flags = condition.substr(0, dot);
			const auto bit = (dot == condition.npos)
				? std::string_view()
				: condition.substr(dot + 1);
			const auto parsed = std::from_chars(
				bit.data(),
				bit.data() + bit.size(),
				param.flagBit);
			if (bit.empty()
				|| parsed.ptr != bit.data() + bit.size()
				|| param.flagBit < 0
				|| param.flagBit > 31) {
				Fail("bad flag condition \"" + std::string(token) + "\"");
			}
			for (auto j = size_t(0); j != result.params.size(); ++j) {
				const auto &earlier = result.params[j];
				if (earlier.name == flags
					&& earlier.type.kind == TypeRef::Kind::Flags) {
					param.flagParam = int(j);
				}
			}
			if (param.flagParam < 0) {
				Fail("unknown flags field in \"" + std::string(token) + "\"");
			}
			type = type.substr(question + 1);
		}
		param.type = ParseTypeOrFail(type, result.generic);
		if (param.type.kind == TypeRef::Kind::Generic) {
			Fail("generic param must be !" + result.generic);
		}
		for (const auto &earlier : result.params) {
			if (earlier.name == param.name) {
				Fail("duplicate param \"" + param.name + "\"");
			}
		}
		result.params.push_back(std::move(param));
	}
	result.resultType = std::string(Trim(statement.substr(equals + 1)));
	const auto resultType = ParseTypeOrFail(result.resultType, result.generic);
	if (!function && resultType.kind != TypeRef::Kind::Boxed
		&& resultType.kind != TypeRef::Kind::Bool) {
		Fail("constructor of a non-boxed type \"" + result.resultType + "\"");
	}
	return result;
}

// A declaration of a builtin, like `int ? = Int;` or `vector ... [ t ]`.
bool IsBuiltinDeclaration(std::string_view statement) {
	for (const auto token : Tokens(statement)) {
		if (token == "?" || token.find('[') != token.npos) {
			return true;
		}
	}
	return false;
}

void Validate(const Schema &schema, const TypeRef &type, const Combinator &owner) {
	using Kind = TypeRef::Kind;
	if (type.kind == Kind::Vector) {
		Validate(schema, type.args[0], owner);
	} else if (type.kind == Kind::Boxed
		&& schema.constructorsOf(type.name).empty()) {
		Fail(owner.name + ": unknown type \"" + type.name + "\"");
	} else if (type.kind == Kind::Bare && !schema.bareConstructor(type.name)) {
		Fail(owner.name + ": unknown bare type \"" + type.name + "\"");
	}
}

} // namespace

Result<TypeRef> ParseType(std::string_view text, std::string_view generic) {
	try {
		return { ParseTypeOrFail(Trim(text), generic), {} };
	} catch (const Failure &failure) {
		return Result<TypeRef>::Fail(failure.text);
	}
}

uint32_t Schema::ComputeId(std::string_view line) {
	// Mirrors lib_tl/tl/generate_tl.py, including its single-pass "  " fold.
	line = Trim(line.substr(0, line.find(';')));
	const auto equals = line.find('=');
	const auto head = line.substr(0, line.find_first_of(" \t=#"));
	const auto afterId = std::min(
		line.find_first_of(" \t=", head.size()),
		equals);
	auto clean = std::string(head)
		+ std::string(line.substr(afterId, equals - afterId))
		+ "= "
		+ std::string(Trim(line.substr(std::min(equals + 1, line.size()))));
	static const auto trueFlag = std::regex(
		R"( [a-zA-Z0-9_]+:flags2?\.[0-9]+\?true)");
	clean = std::regex_replace(clean, trueFlag, "");
	for (auto &ch : clean) {
		if (ch == '<' || ch == '>') {
			ch = ' ';
		}
	}
	const auto replace = [&](std::string_view from, std::string_view to) {
		auto result = std::string();
		auto i = size_t(0);
		for (auto found = clean.find(from); found != clean.npos; found = clean.find(from, i)) {
			result.append(clean, i, found - i).append(to);
			i = found + from.size();
		}
		clean = result.append(clean, i);
	};
	replace("  ", " ");
	if (clean.starts_with(' ')) {
		clean.erase(0, 1);
	}
	if (clean.ends_with(' ')) {
		clean.pop_back();
	}
	replace(":bytes ", ":string ");
	replace("?bytes ", "?string ");
	replace("{", "");
	replace("}", "");
	return Crc32(clean);
}

Result<Schema> Schema::Parse(std::string_view text) {
	auto schema = Schema();
	auto function = false;
	auto pending = std::string();
	auto pendingLine = 0;
	auto lineNumber = 0;
	try {
		while (!text.empty()) {
			++lineNumber;
			const auto newline = text.find('\n');
			auto line = text.substr(0, newline);
			text = (newline == text.npos) ? std::string_view() : text.substr(newline + 1);
			line = Trim(line.substr(0, line.find("//")));
			if (line.empty()) {
				continue;
			} else if (line.starts_with("---")) {
				if (!Trim(pending).empty()) {
					Fail("unterminated declaration");
				} else if (line == "---functions---") {
					function = true;
				} else if (line == "---types---") {
					function = false;
				} else {
					Fail("unknown section \"" + std::string(line) + "\"");
				}
				continue;
			}
			if (Trim(pending).empty()) {
				pendingLine = lineNumber;
			}
			pending.append(" ").append(line);
			for (auto semicolon = pending.find(';');
				semicolon != pending.npos;
				semicolon = pending.find(';')) {
				const auto statement = std::string(Trim(std::string_view(pending).substr(0, semicolon)));
				pending.erase(0, semicolon + 1);
				if (!statement.empty() && !IsBuiltinDeclaration(statement)) {
					auto combinator = ParseCombinator(statement, function);
					const auto error = schema.add(std::move(combinator));
					if (!error.empty()) {
						Fail(error);
					}
				}
				pendingLine = lineNumber;
			}
		}
		if (!Trim(pending).empty()) {
			lineNumber = pendingLine;
			Fail("unterminated declaration");
		}
		lineNumber = 0;
		for (const auto &combinator : schema._combinators) {
			for (const auto &param : combinator.params) {
				Validate(schema, param.type, combinator);
			}
			if (combinator.function) {
				Validate(
					schema,
					ParseTypeOrFail(combinator.resultType, combinator.generic),
					combinator);
			}
		}
	} catch (const Failure &failure) {
		return Result<Schema>::Fail(lineNumber
			? ("line " + std::to_string(pendingLine) + ": " + failure.text)
			: failure.text);
	}
	return { std::move(schema), {} };
}

std::string Schema::add(Combinator &&combinator) {
	const auto index = _combinators.size();
	auto &byName = combinator.function ? _methodsByName : _constructorsByName;
	auto &byId = combinator.function ? _methodsById : _constructorsById;
	if (!byName.emplace(combinator.name, index).second) {
		return "duplicate name \"" + combinator.name + "\"";
	} else if (!byId.emplace(combinator.id, index).second) {
		byName.erase(combinator.name);
		return "duplicate id of \"" + combinator.name + "\"";
	}
	if (!combinator.function) {
		_types[combinator.resultType].push_back(index);
	}
	_combinators.push_back(std::move(combinator));
	return {};
}

const Combinator *Schema::constructor(std::string_view name) const {
	const auto i = _constructorsByName.find(std::string(name));
	return (i == end(_constructorsByName)) ? nullptr : &_combinators[i->second];
}

const Combinator *Schema::constructor(uint32_t id) const {
	const auto i = _constructorsById.find(id);
	return (i == end(_constructorsById)) ? nullptr : &_combinators[i->second];
}

const Combinator *Schema::method(std::string_view name) const {
	const auto i = _methodsByName.find(std::string(name));
	return (i == end(_methodsByName)) ? nullptr : &_combinators[i->second];
}

const Combinator *Schema::method(uint32_t id) const {
	const auto i = _methodsById.find(id);
	return (i == end(_methodsById)) ? nullptr : &_combinators[i->second];
}

const Combinator *Schema::bareConstructor(std::string_view name) const {
	if (const auto result = constructor(name)) {
		return result;
	}
	const auto i = _types.find(std::string(name));
	return (i != end(_types) && i->second.size() == 1)
		? &_combinators[i->second.front()]
		: nullptr;
}

std::vector<const Combinator*> Schema::constructorsOf(
		std::string_view type) const {
	auto result = std::vector<const Combinator*>();
	if (const auto i = _types.find(std::string(type)); i != end(_types)) {
		for (const auto index : i->second) {
			result.push_back(&_combinators[index]);
		}
	}
	return result;
}

std::optional<std::string> Schema::MethodReturnType(
		std::string_view name) const {
	const auto found = method(name);
	return found ? std::make_optional(found->resultType) : std::nullopt;
}

} // namespace Luxury::Plugins::Tl
