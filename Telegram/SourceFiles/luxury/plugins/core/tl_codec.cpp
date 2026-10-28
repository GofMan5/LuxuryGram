/*
This file is part of LuxuryGram, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "luxury/plugins/core/tl_codec.h"

#include <charconv>
#include <cstring>
#include <limits>
#include <system_error>

namespace Luxury::Plugins::Tl {
namespace {

using json = nlohmann::json;
using Kind = TypeRef::Kind;

constexpr auto kVectorId = 0x1cb5c415U;
constexpr auto kBoolTrueId = 0x997275b5U;
constexpr auto kBoolFalseId = 0xbc799737U;
constexpr auto kGzipPackedId = 0x3072cfa1U;
constexpr auto kMaxStringSize = size_t(0x1000000);

// Rendered only when something fails, so the happy path builds no strings.
struct Path {
	const Path *parent = nullptr;
	std::string_view key;
	size_t index = 0;
	bool isIndex = false;

	[[nodiscard]] Path field(std::string_view name) const {
		return { this, name, 0, false };
	}
	[[nodiscard]] Path item(size_t i) const {
		return { this, {}, i, true };
	}
	[[nodiscard]] std::string render() const {
		auto result = parent ? parent->render() : std::string();
		if (isIndex) {
			result += '[' + std::to_string(index) + ']';
		} else if (!key.empty()) {
			result += (result.empty() ? "" : ".") + std::string(key);
		}
		return result;
	}
};

struct Failure {
	std::string text;
};

[[noreturn]] void Fail(const Path &path, const std::string &text) {
	const auto where = path.render();
	throw Failure{ where.empty() ? text : (where + ": " + text) };
}

std::string Hex(uint32_t value) {
	char buffer[11] = "0x";
	const auto result = std::to_chars(buffer + 2, buffer + 10, value, 16);
	return std::string(buffer, result.ptr);
}

std::string Quoted(std::string_view text) {
	return '"' + std::string(text) + '"';
}

constexpr auto kBase64 = std::string_view(
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");

std::string Base64(std::string_view data) {
	auto result = std::string();
	result.reserve((data.size() + 2) / 3 * 4);
	for (auto i = size_t(0); i < data.size(); i += 3) {
		const auto left = data.size() - i;
		auto chunk = uint32_t(static_cast<unsigned char>(data[i])) << 16;
		if (left > 1) {
			chunk |= uint32_t(static_cast<unsigned char>(data[i + 1])) << 8;
		}
		if (left > 2) {
			chunk |= uint32_t(static_cast<unsigned char>(data[i + 2]));
		}
		result += kBase64[(chunk >> 18) & 0x3F];
		result += kBase64[(chunk >> 12) & 0x3F];
		result += (left > 1) ? kBase64[(chunk >> 6) & 0x3F] : '=';
		result += (left > 2) ? kBase64[chunk & 0x3F] : '=';
	}
	return result;
}

std::optional<std::string> FromBase64(std::string_view text) {
	while (!text.empty() && text.back() == '=') {
		text.remove_suffix(1);
	}
	if (text.size() % 4 == 1) {
		return std::nullopt;
	}
	auto result = std::string();
	auto chunk = uint32_t(0);
	auto bits = 0;
	for (const auto ch : text) {
		const auto value = kBase64.find(ch);
		if (value == std::string_view::npos) {
			return std::nullopt;
		}
		chunk = (chunk << 6) | uint32_t(value);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			result += static_cast<char>((chunk >> bits) & 0xFF);
		}
	}
	return result;
}

// TL strings carry arbitrary bytes; JSON strings must be valid UTF-8.
std::string SanitizeUtf8(std::string_view data) {
	auto result = std::string();
	result.reserve(data.size());
	const auto byte = [&](size_t i) {
		return static_cast<unsigned char>(data[i]);
	};
	for (auto i = size_t(0); i < data.size();) {
		const auto lead = byte(i);
		auto length = 0;
		auto min = uint32_t(0);
		auto code = uint32_t(0);
		if (lead < 0x80) {
			result += static_cast<char>(lead);
			++i;
			continue;
		} else if ((lead & 0xE0) == 0xC0) {
			length = 2, min = 0x80, code = lead & 0x1F;
		} else if ((lead & 0xF0) == 0xE0) {
			length = 3, min = 0x800, code = lead & 0x0F;
		} else if ((lead & 0xF8) == 0xF0) {
			length = 4, min = 0x10000, code = lead & 0x07;
		}
		auto valid = (length > 0) && (i + length <= data.size());
		for (auto k = 1; valid && k < length; ++k) {
			valid = ((byte(i + k) & 0xC0) == 0x80);
			code = (code << 6) | (byte(i + k) & 0x3F);
		}
		valid = valid
			&& code >= min
			&& code <= 0x10FFFF
			&& (code < 0xD800 || code > 0xDFFF);
		if (valid) {
			result.append(data.substr(i, length));
			i += length;
		} else {
			result += "\xEF\xBF\xBD";
			++i;
		}
	}
	return result;
}

class Encoder final {
public:
	explicit Encoder(const Schema &schema) : _schema(schema) {
	}

	void method(const json &call, const Path &path, int depth) {
		const auto name = constructorName(call, path, depth);
		const auto combinator = _schema.method(name);
		if (!combinator) {
			Fail(path.field("_"), "unknown method " + Quoted(name));
		}
		put(combinator->id);
		fields(*combinator, call, path, depth);
	}

	void encode(const TypeRef &type, const json &data) {
		value(type, data, Path(), 0);
	}

	[[nodiscard]] std::vector<uint32_t> take() {
		return std::move(_out);
	}

private:
	void put(uint32_t value) {
		_out.push_back(value);
	}

	void putBytes(std::string_view data) {
		auto buffer = std::string();
		if (data.size() < 254) {
			buffer += static_cast<char>(data.size());
		} else {
			buffer += static_cast<char>(254);
			buffer += static_cast<char>(data.size() & 0xFF);
			buffer += static_cast<char>((data.size() >> 8) & 0xFF);
			buffer += static_cast<char>((data.size() >> 16) & 0xFF);
		}
		buffer.append(data);
		buffer.resize((buffer.size() + 3) / 4 * 4, '\0');
		putRaw(buffer);
	}

	void putRaw(std::string_view data) {
		for (auto i = size_t(0); i < data.size(); i += 4) {
			auto word = uint32_t(0);
			for (auto k = size_t(0); k != 4; ++k) {
				word |= uint32_t(static_cast<unsigned char>(data[i + k])) << (8 * k);
			}
			put(word);
		}
	}

	std::string constructorName(const json &value, const Path &path, int depth) {
		if (depth > kMaxDepth) {
			Fail(path, "nesting deeper than " + std::to_string(kMaxDepth));
		} else if (!value.is_object()) {
			Fail(path, "expected an object");
		}
		const auto i = value.find("_");
		if (i == value.end() || !i->is_string()) {
			Fail(path.field("_"), "missing constructor name");
		}
		return i->get<std::string>();
	}

	void fields(
			const Combinator &combinator,
			const json &object,
			const Path &path,
			int depth) {
		const auto &params = combinator.params;
		const auto find = [&](std::string_view key) -> const Param* {
			for (const auto &param : params) {
				if (param.name == key) {
					return &param;
				}
			}
			return nullptr;
		};
		for (const auto &[key, value] : object.items()) {
			if (key != "_" && !find(key)) {
				Fail(path.field(key), "unknown field of " + combinator.name);
			}
		}
		const auto present = [&](const Param &param) {
			const auto i = object.find(param.name);
			if (i == object.end() || i->is_null()) {
				return false;
			} else if (param.type.kind != Kind::True) {
				return true;
			} else if (!i->is_boolean()) {
				Fail(path.field(param.name), "expected true or false");
			}
			return i->get<bool>();
		};
		auto flags = std::vector<uint32_t>(params.size());
		for (const auto &param : params) {
			if (param.flagParam >= 0 && present(param)) {
				flags[param.flagParam] |= (1U << param.flagBit);
			}
		}
		for (auto i = size_t(0); i != params.size(); ++i) {
			const auto &param = params[i];
			const auto field = path.field(param.name);
			if (param.type.kind == Kind::Flags) {
				put(flags[i]);
				continue;
			} else if (param.flagParam >= 0
				&& !(flags[param.flagParam] & (1U << param.flagBit))) {
				continue;
			} else if (param.type.kind == Kind::True) {
				continue;
			} else if (!present(param)) {
				Fail(field, (param.flagParam >= 0)
					? ("required by "
						+ params[param.flagParam].name
						+ '.'
						+ std::to_string(param.flagBit)
						+ " set by another field")
					: "missing field of " + combinator.name);
			}
			value(param.type, object[param.name], field, depth + 1);
		}
	}

	void value(const TypeRef &type, const json &value, const Path &path, int depth) {
		switch (type.kind) {
		case Kind::Int: {
			const auto fits = value.is_number_unsigned()
				? (value.get<uint64_t>() <= uint64_t(std::numeric_limits<int32_t>::max()))
				: (value.is_number_integer()
					&& value.get<int64_t>() >= std::numeric_limits<int32_t>::min()
					&& value.get<int64_t>() <= std::numeric_limits<int32_t>::max());
			if (!fits) {
				Fail(path, "expected a 32-bit int");
			}
			put(uint32_t(int32_t(value.get<int64_t>())));
		} break;
		case Kind::Long: {
			auto number = uint64_t();
			if (value.is_number_unsigned()) {
				number = value.get<uint64_t>();
			} else if (value.is_number_integer()) {
				number = uint64_t(value.get<int64_t>());
			} else if (!value.is_string() || !parseLong(value.get_ref<const std::string&>(), number)) {
				Fail(path, "expected a long as a decimal string");
			}
			put(uint32_t(number & 0xFFFFFFFFULL));
			put(uint32_t(number >> 32));
		} break;
		case Kind::Double: {
			if (!value.is_number()) {
				Fail(path, "expected a number");
			}
			const auto number = value.get<double>();
			auto bits = uint64_t();
			std::memcpy(&bits, &number, sizeof(bits));
			put(uint32_t(bits & 0xFFFFFFFFULL));
			put(uint32_t(bits >> 32));
		} break;
		case Kind::String:
			if (!value.is_string()) {
				Fail(path, "expected a string");
			}
			putString(value.get_ref<const std::string&>(), path);
			break;
		case Kind::Bytes: {
			const auto decoded = value.is_string()
				? FromBase64(value.get_ref<const std::string&>())
				: std::nullopt;
			if (!decoded) {
				Fail(path, "expected base64 bytes");
			}
			putString(*decoded, path);
		} break;
		case Kind::Int128:
		case Kind::Int256: {
			const auto size = (type.kind == Kind::Int128) ? 16 : 32;
			auto raw = value.is_string()
				? fromHex(value.get_ref<const std::string&>())
				: std::nullopt;
			if (!raw || raw->size() != size_t(size)) {
				Fail(path, "expected " + std::to_string(size * 2) + " hex digits");
			}
			putRaw(*raw);
		} break;
		case Kind::Bool:
			if (!value.is_boolean()) {
				Fail(path, "expected true or false");
			}
			put(value.get<bool>() ? kBoolTrueId : kBoolFalseId);
			break;
		case Kind::True:
		case Kind::Flags:
			break;
		case Kind::Vector: {
			if (depth > kMaxDepth) {
				Fail(path, "nesting deeper than " + std::to_string(kMaxDepth));
			} else if (!value.is_array()) {
				Fail(path, "expected an array");
			}
			if (!type.bare) {
				put(kVectorId);
			}
			put(uint32_t(value.size()));
			for (auto i = size_t(0); i != value.size(); ++i) {
				this->value(type.args[0], value[i], path.item(i), depth + 1);
			}
		} break;
		case Kind::Boxed: {
			const auto name = constructorName(value, path, depth);
			const auto combinator = _schema.constructor(name);
			if (!combinator) {
				Fail(path.field("_"), "unknown constructor " + Quoted(name));
			} else if (combinator->resultType != type.name) {
				Fail(path.field("_"), Quoted(name) + " is not a " + type.name);
			}
			put(combinator->id);
			fields(*combinator, value, path, depth);
		} break;
		case Kind::Bare: {
			const auto combinator = _schema.bareConstructor(type.name);
			if (depth > kMaxDepth) {
				Fail(path, "nesting deeper than " + std::to_string(kMaxDepth));
			} else if (!value.is_object()) {
				Fail(path, "expected an object");
			} else if (!combinator) {
				Fail(path, "unknown bare type " + Quoted(type.name));
			}
			const auto i = value.find("_");
			if (i != value.end() && *i != combinator->name) {
				Fail(path.field("_"), "expected " + Quoted(combinator->name));
			}
			fields(*combinator, value, path, depth);
		} break;
		case Kind::Query:
			method(value, path, depth);
			break;
		case Kind::Generic:
			Fail(path, "unbound generic type " + type.name);
		}
	}

	void putString(const std::string &data, const Path &path) {
		if (data.size() >= kMaxStringSize) {
			Fail(path, "longer than 16 MiB");
		}
		putBytes(data);
	}

	static bool parseLong(const std::string &text, uint64_t &result) {
		const auto begin = text.data();
		const auto end = begin + text.size();
		const auto whole = [&](auto &value) {
			const auto parsed = std::from_chars(begin, end, value);
			return !text.empty() && parsed.ec == std::errc() && parsed.ptr == end;
		};
		auto signedValue = int64_t();
		if (whole(signedValue)) {
			result = uint64_t(signedValue);
			return true;
		}
		return text[0] != '-' && whole(result);
	}

	static std::optional<std::string> fromHex(const std::string &text) {
		if (text.size() % 2) {
			return std::nullopt;
		}
		auto result = std::string();
		for (auto i = size_t(0); i != text.size(); i += 2) {
			auto byte = 0U;
			const auto parsed = std::from_chars(
				text.data() + i,
				text.data() + i + 2,
				byte,
				16);
			if (parsed.ptr != text.data() + i + 2) {
				return std::nullopt;
			}
			result += static_cast<char>(byte);
		}
		return result;
	}

	const Schema &_schema;
	std::vector<uint32_t> _out;

};

class Decoder final {
public:
	Decoder(
		const Schema &schema,
		const Inflate &inflate,
		const uint32_t *data,
		size_t words)
	: _schema(schema)
	, _inflate(inflate)
	, _data(data)
	, _size(data ? words : 0)
	, _budget(_size * 2 + 16) {
	}

	[[nodiscard]] json value(const TypeRef &type, const Path &path, int depth) {
		if (depth > kMaxDepth) {
			Fail(path, "nesting deeper than " + std::to_string(kMaxDepth));
		}
		const auto boxed = (type.kind == Kind::Boxed)
			|| (type.kind == Kind::Bool)
			|| (type.kind == Kind::Vector && !type.bare);
		if (boxed && _position < _size && _data[_position] == kGzipPackedId) {
			++_position;
			return gzipped(type, path, depth);
		}
		switch (type.kind) {
		case Kind::Int:
			return int32_t(word(path));
		case Kind::Long:
			return std::to_string(int64_t(longWord(path)));
		case Kind::Double: {
			const auto bits = longWord(path);
			auto result = double();
			std::memcpy(&result, &bits, sizeof(result));
			return result;
		}
		case Kind::String:
			return SanitizeUtf8(bytes(path));
		case Kind::Bytes:
			return Base64(bytes(path));
		case Kind::Int128:
		case Kind::Int256: {
			const auto words = (type.kind == Kind::Int128) ? 4 : 8;
			auto result = std::string();
			for (auto i = 0; i != words; ++i) {
				const auto value = word(path);
				for (auto k = 0; k != 4; ++k) {
					constexpr auto kDigits = std::string_view("0123456789abcdef");
					const auto byte = (value >> (8 * k)) & 0xFF;
					result += kDigits[byte >> 4];
					result += kDigits[byte & 0x0F];
				}
			}
			return result;
		}
		case Kind::Bool: {
			const auto id = word(path);
			if (id != kBoolTrueId && id != kBoolFalseId) {
				Fail(path, "expected Bool, got " + Hex(id));
			}
			return (id == kBoolTrueId);
		}
		case Kind::True:
			return true;
		case Kind::Vector: {
			if (!type.bare && word(path) != kVectorId) {
				Fail(path, "expected a vector");
			}
			const auto count = word(path);
			if (count > _size - _position) {
				Fail(path, "vector of " + std::to_string(count) + " is longer than the data");
			}
			if (count > _budget) {
				Fail(path, "too many vector elements");
			}
			_budget -= count;
			auto result = json::array();
			for (auto i = size_t(0); i != count; ++i) {
				result.push_back(value(type.args[0], path.item(i), depth + 1));
			}
			return result;
		}
		case Kind::Boxed: {
			const auto id = word(path);
			const auto combinator = _schema.constructor(id);
			if (!combinator) {
				Fail(path, "unknown constructor " + Hex(id));
			} else if (!type.name.empty() && combinator->resultType != type.name) {
				Fail(path, Quoted(combinator->name) + " is not a " + type.name);
			}
			return object(*combinator, path, depth);
		}
		case Kind::Bare: {
			const auto combinator = _schema.bareConstructor(type.name);
			if (!combinator) {
				Fail(path, "unknown bare type " + Quoted(type.name));
			}
			return object(*combinator, path, depth);
		}
		case Kind::Flags:
			return word(path);
		case Kind::Query:
		case Kind::Generic:
			break;
		}
		Fail(path, "cannot decode a generic value");
	}

	void finish(const Path &path) const {
		if (_position != _size) {
			Fail(path, std::to_string(_size - _position) + " words left over");
		}
	}

private:
	[[nodiscard]] uint32_t word(const Path &path) {
		if (_position >= _size) {
			Fail(path, "unexpected end of data");
		}
		return _data[_position++];
	}

	[[nodiscard]] uint64_t longWord(const Path &path) {
		const auto low = uint64_t(word(path));
		return low | (uint64_t(word(path)) << 32);
	}

	[[nodiscard]] std::string bytes(const Path &path) {
		const auto first = word(path);
		const auto marker = first & 0xFF;
		if (marker > 254) {
			Fail(path, "bad string length marker");
		}
		const auto large = (marker == 254);
		const auto length = size_t(large ? (first >> 8) : marker);
		const auto offset = size_t(large ? 4 : 1);
		const auto words = (offset + length + 3) / 4;
		if (words - 1 > _size - _position) {
			Fail(path, "string is longer than the data");
		}
		const auto start = _position - 1;
		auto result = std::string(length, '\0');
		for (auto i = size_t(0); i != length; ++i) {
			const auto at = offset + i;
			result[i] = static_cast<char>((_data[start + at / 4] >> (8 * (at % 4))) & 0xFF);
		}
		_position = start + words;
		return result;
	}

	[[nodiscard]] json gzipped(const TypeRef &type, const Path &path, int depth) {
		const auto packed = bytes(path);
		if (!_inflate) {
			Fail(path, "gzip_packed without an inflater");
		}
		const auto unpacked = _inflate(packed);
		if (!unpacked || unpacked->empty() || unpacked->size() % 4) {
			Fail(path, "corrupt gzip_packed");
		}
		auto words = std::vector<uint32_t>(unpacked->size() / 4);
		std::memcpy(words.data(), unpacked->data(), unpacked->size());
		auto inner = Decoder(_schema, _inflate, words.data(), words.size());
		auto result = inner.value(type, path, depth + 1);
		inner.finish(path);
		return result;
	}

	[[nodiscard]] json object(const Combinator &combinator, const Path &path, int depth) {
		auto result = json::object();
		result["_"] = combinator.name;
		auto flags = std::vector<uint32_t>(combinator.params.size());
		for (auto i = size_t(0); i != combinator.params.size(); ++i) {
			const auto &param = combinator.params[i];
			const auto field = path.field(param.name);
			if (param.type.kind == Kind::Flags) {
				flags[i] = word(field);
			} else if (param.flagParam < 0
				|| (flags[param.flagParam] & (1U << param.flagBit))) {
				result[param.name] = value(param.type, field, depth + 1);
			}
		}
		return result;
	}

	const Schema &_schema;
	const Inflate &_inflate;
	const uint32_t *_data = nullptr;
	size_t _size = 0;
	size_t _position = 0;
	size_t _budget = 0; // Zero-word elements must not make decoding quadratic.

};

template <typename Callback>
auto Guard(Callback &&callback) -> Result<decltype(callback())> {
	using Value = decltype(callback());
	try {
		return { callback(), {} };
	} catch (const Failure &failure) {
		return Result<Value>::Fail(failure.text);
	} catch (const std::exception &e) {
		return Result<Value>::Fail(e.what());
	}
}

} // namespace

Result<std::vector<uint32_t>> EncodeMethod(
		const Schema &schema,
		const json &call) {
	return Guard([&] {
		auto encoder = Encoder(schema);
		encoder.method(call, Path(), 0);
		return encoder.take();
	});
}

Result<std::vector<uint32_t>> EncodeValue(
		const Schema &schema,
		std::string_view type,
		const json &value) {
	const auto parsed = ParseType(type);
	if (!parsed) {
		return Result<std::vector<uint32_t>>::Fail(parsed.error);
	}
	return Guard([&] {
		auto encoder = Encoder(schema);
		encoder.encode(*parsed.value, value);
		return encoder.take();
	});
}

std::optional<std::string> CallReturnType(
		const Schema &schema,
		const json &call) {
	auto current = &call;
	for (auto depth = 0; depth <= kMaxDepth; ++depth) {
		const auto name = current->is_object() ? current->find("_") : current->end();
		if (name == current->end() || !name->is_string()) {
			return std::nullopt;
		}
		const auto method = schema.method(name->get_ref<const std::string&>());
		if (!method) {
			return std::nullopt;
		} else if (method->generic.empty()) {
			return method->resultType;
		}
		auto next = static_cast<const json*>(nullptr);
		for (const auto &param : method->params) {
			if (param.type.kind == Kind::Query && current->contains(param.name)) {
				next = &(*current)[param.name];
			}
		}
		if (!next) {
			return std::nullopt;
		}
		current = next;
	}
	return std::nullopt;
}

Result<json> DecodeResult(
		const Schema &schema,
		std::string_view returnType,
		const uint32_t *data,
		size_t words,
		const Inflate &inflate) {
	const auto type = ParseType(returnType);
	if (!type) {
		return Result<json>::Fail(type.error);
	}
	return Guard([&] {
		auto decoder = Decoder(schema, inflate, data, words);
		auto result = decoder.value(*type.value, Path(), 0);
		decoder.finish(Path());
		return result;
	});
}

Result<json> DecodeObject(
		const Schema &schema,
		const uint32_t *data,
		size_t words,
		const Inflate &inflate) {
	return Guard([&] {
		auto decoder = Decoder(schema, inflate, data, words);
		auto result = decoder.value(TypeRef(), Path(), 0);
		decoder.finish(Path());
		return result;
	});
}

} // namespace Luxury::Plugins::Tl
