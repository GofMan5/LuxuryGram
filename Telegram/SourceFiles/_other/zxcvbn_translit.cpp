/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/passcode_strength_translit.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

constexpr auto kMinimumMappedLength = std::size_t(2);

struct Decoded {
	char32_t point = 0;
	std::size_t length = 0;
};

[[nodiscard]] Decoded DecodeUtf8(std::string_view text, std::size_t from) {
	const auto byteAt = [&](std::size_t index) {
		return (index < text.size())
			? static_cast<unsigned char>(text[index])
			: static_cast<unsigned char>(0);
	};
	const auto first = byteAt(from);
	auto length = std::size_t(0);
	auto minimum = char32_t(0);
	auto point = char32_t(0);
	if (first < 0x80) {
		return { first, 1 };
	} else if (first >= 0xC2 && first <= 0xDF) {
		length = 2;
		minimum = 0x80;
		point = char32_t(first & 0x1F);
	} else if (first >= 0xE0 && first <= 0xEF) {
		length = 3;
		minimum = 0x800;
		point = char32_t(first & 0x0F);
	} else if (first >= 0xF0 && first <= 0xF4) {
		length = 4;
		minimum = 0x10000;
		point = char32_t(first & 0x07);
	} else {
		return {};
	}
	for (auto i = std::size_t(1); i != length; ++i) {
		const auto next = byteAt(from + i);
		if (next < 0x80 || next > 0xBF) {
			return {};
		}
		point = char32_t((point << 6) | (next & 0x3F));
	}
	const auto surrogate = (point >= 0xD800 && point <= 0xDFFF);
	if (point < minimum || surrogate || point > 0x10FFFF) {
		return {};
	}
	return { point, length };
}

[[nodiscard]] std::string MapWord(const std::string &word) {
	auto mapped = std::string();
	for (auto from = std::size_t(0); from < word.size();) {
		const auto decoded = DecodeUtf8(word, from);
		if (!decoded.length) {
			return {};
		}
		from += decoded.length;
		if (decoded.point < 0x80) {
			mapped.push_back(char(decoded.point));
			continue;
		}
		const auto latin = Ui::TransliterateCyrillic(
			Ui::LowercaseCyrillic(decoded.point));
		if (!latin) {
			return {};
		}
		mapped.append(latin);
	}
	return (mapped.size() >= kMinimumMappedLength)
		? mapped
		: std::string();
}

[[nodiscard]] bool IsDroppedMarkup(std::string_view word) {
	return (word == "chffffff")
		|| (word == "fntahoma")
		|| (word == "b1")
		|| (word == "i0");
}

[[nodiscard]] int Fail(const char *action, const char *path) {
	std::fprintf(stderr, "zxcvbn_translit: cannot %s %s\n", action, path);
	return 1;
}

// The whole stream transformation as one pure call. Keeping the loop here lets
// an optimizer (or a native port) cross the boundary once per dictionary
// instead of once per word. Empty result means no word was mapped.
// Optional Rust implementation behind a binary C ABI. The default path stays
// C++; set LUXURY_ZXCVBN_RUST=1 and LUXURY_ZXCVBN_RUST_DLL=<library> to use it.
// A missing or incompatible library silently falls back to TransformDictionary.
[[nodiscard]] std::optional<std::string> TryRustTransform(const std::string &input) {
	const auto enabled = std::getenv("LUXURY_ZXCVBN_RUST");
	if (!enabled || std::string_view(enabled) != "1") {
		return std::nullopt;
	}
	const auto path = std::getenv("LUXURY_ZXCVBN_RUST_DLL");
	if (!path || !*path) {
		return std::nullopt;
	}
#ifdef _WIN32
	const auto library = LoadLibraryA(path);
#else
	const auto library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
	if (!library) {
		return std::nullopt;
	}
	using Transform = uint8_t *(*)(const uint8_t *, size_t, size_t *);
	using Release = void (*)(uint8_t *, size_t);
#ifdef _WIN32
	const auto transform = reinterpret_cast<Transform>(
		reinterpret_cast<void *>(GetProcAddress(library, "luxury_zxcvbn_transform")));
	const auto release = reinterpret_cast<Release>(
		reinterpret_cast<void *>(GetProcAddress(library, "luxury_zxcvbn_free")));
#else
	const auto transform = reinterpret_cast<Transform>(
		dlsym(library, "luxury_zxcvbn_transform"));
	const auto release = reinterpret_cast<Release>(
		dlsym(library, "luxury_zxcvbn_free"));
#endif
	if (!transform || !release) {
#ifdef _WIN32
		FreeLibrary(library);
#else
		dlclose(library);
#endif
		return std::nullopt;
	}
	auto length = size_t(0);
	const auto bytes = transform(
		reinterpret_cast<const uint8_t *>(input.data()),
		input.size(),
		&length);
	if (!bytes) {
#ifdef _WIN32
		FreeLibrary(library);
#else
		dlclose(library);
#endif
		return std::string();
	}
	auto result = std::string(reinterpret_cast<const char *>(bytes), length);
	release(bytes, length);
#ifdef _WIN32
	FreeLibrary(library);
#else
	dlclose(library);
#endif
	return result;
}

[[nodiscard]] std::string TransformDictionary(const std::string &input) {
	auto output = std::string();
	auto line = std::string();
	for (auto pos = std::size_t(0); pos < input.size();) {
		const auto end = input.find('\n', pos);
		if (end == std::string::npos) {
			line.assign(input, pos, input.size() - pos);
			pos = input.size();
		} else {
			line.assign(input, pos, end - pos);
			pos = end + 1;
		}
		auto word = std::string_view(line);
		if (!word.empty() && word.back() == '\r') {
			word.remove_suffix(1);
		}
		word = word.substr(0, word.find_first_of(" \t"));
		if (!word.empty()) {
			auto mapped = MapWord(std::string(word));
			if (!mapped.empty() && !IsDroppedMarkup(mapped)) {
				output += mapped;
				output.push_back('\n');
			}
		}
	}
	return output;
}

} // namespace

int main(int argc, char *argv[]) {
	if (argc != 3) {
		std::fprintf(stderr, "usage: zxcvbn_translit <input.txt> <output.txt>\n");
		return 1;
	}
	auto input = std::ifstream(argv[1], std::ios::binary);
	if (!input) {
		return Fail("read", argv[1]);
	}
	const auto tmpPath = std::string(argv[2]) + ".tmp";
	std::remove(tmpPath.c_str());
	auto output = std::ofstream(tmpPath, std::ios::binary);
	if (!output) {
		return Fail("write", argv[2]);
	}
	const auto bytes = std::string(
		std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
	if (input.bad()) {
		output.close();
		std::remove(tmpPath.c_str());
		return Fail("read", argv[1]);
	}
	auto rust = TryRustTransform(bytes);
	const auto mapped = rust ? std::move(*rust) : TransformDictionary(bytes);
	output << mapped;
	output.close();
	if (!output) {
		std::remove(tmpPath.c_str());
		return Fail("write", argv[2]);
	}
	if (mapped.empty()) {
		std::remove(tmpPath.c_str());
		std::fprintf(
			stderr,
			"zxcvbn_translit: no words mapped from %s\n",
			argv[1]);
		return 1;
	}
	std::remove(argv[2]);
	if (std::rename(tmpPath.c_str(), argv[2]) != 0) {
		std::remove(tmpPath.c_str());
		return Fail("write", argv[2]);
	}
	return 0;
}
