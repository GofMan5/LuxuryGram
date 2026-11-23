/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/passcode_strength_translit.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

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
	const auto mapped = TransformDictionary(bytes);
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
