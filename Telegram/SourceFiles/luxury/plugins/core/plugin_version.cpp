// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/core/plugin_version.h"

#include <algorithm>
#include <tuple>

namespace Luxury::Plugins {
namespace {

constexpr auto kMaxNumberDigits = 9;

[[nodiscard]] bool IsDigits(std::string_view text) {
	return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
		return c >= '0' && c <= '9';
	});
}

[[nodiscard]] std::optional<int> ParseNumber(std::string_view text) {
	if (!IsDigits(text)
		|| text.size() > kMaxNumberDigits
		|| (text.size() > 1 && text[0] == '0')) {
		return std::nullopt;
	}
	auto result = 0;
	for (const auto c : text) {
		result = result * 10 + (c - '0');
	}
	return result;
}

[[nodiscard]] bool IsIdentifier(std::string_view text) {
	return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
		return (c >= '0' && c <= '9')
			|| (c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')
			|| c == '-';
	});
}

[[nodiscard]] std::strong_ordering CompareIdentifier(
		const std::string &a,
		const std::string &b) {
	const auto aNumeric = IsDigits(a);
	const auto bNumeric = IsDigits(b);
	if (aNumeric && bNumeric) {
		if (a.size() != b.size()) {
			return a.size() <=> b.size();
		}
		return a.compare(b) <=> 0;
	} else if (aNumeric != bNumeric) {
		return aNumeric
			? std::strong_ordering::less
			: std::strong_ordering::greater;
	}
	return a.compare(b) <=> 0;
}

} // namespace

std::optional<Version> ParseVersion(std::string_view text) {
	const auto plus = text.find('+');
	if (plus != std::string_view::npos) {
		// Build metadata: dot-separated identifiers, ignored in ordering.
		auto build = text.substr(plus + 1);
		while (true) {
			const auto dot = build.find('.');
			if (!IsIdentifier(build.substr(0, dot))) {
				return std::nullopt;
			} else if (dot == std::string_view::npos) {
				break;
			}
			build = build.substr(dot + 1);
		}
		text = text.substr(0, plus);
	}
	auto result = Version();
	const auto dash = text.find('-');
	if (dash != std::string_view::npos) {
		auto rest = text.substr(dash + 1);
		text = text.substr(0, dash);
		while (true) {
			const auto dot = rest.find('.');
			const auto part = rest.substr(0, dot);
			if (!IsIdentifier(part)
				|| (IsDigits(part) && !ParseNumber(part))) {
				return std::nullopt;
			}
			result.prerelease.emplace_back(part);
			if (dot == std::string_view::npos) {
				break;
			}
			rest = rest.substr(dot + 1);
		}
	}
	int *parts[] = { &result.major, &result.minor, &result.patch };
	auto index = 0;
	while (true) {
		const auto dot = text.find('.');
		const auto number = ParseNumber(text.substr(0, dot));
		if (!number || index == 3) {
			return std::nullopt;
		}
		*parts[index++] = *number;
		if (dot == std::string_view::npos) {
			break;
		}
		text = text.substr(dot + 1);
	}
	return result;
}

std::strong_ordering operator<=>(const Version &a, const Version &b) {
	const auto release = std::tie(a.major, a.minor, a.patch)
		<=> std::tie(b.major, b.minor, b.patch);
	if (release != 0) {
		return release;
	} else if (a.prerelease.empty() != b.prerelease.empty()) {
		return a.prerelease.empty()
			? std::strong_ordering::greater
			: std::strong_ordering::less;
	}
	const auto count = std::min(a.prerelease.size(), b.prerelease.size());
	for (auto i = std::size_t(); i != count; ++i) {
		const auto c = CompareIdentifier(a.prerelease[i], b.prerelease[i]);
		if (c != 0) {
			return c;
		}
	}
	return a.prerelease.size() <=> b.prerelease.size();
}

bool operator==(const Version &a, const Version &b) {
	return (a <=> b) == 0;
}

} // namespace Luxury::Plugins
