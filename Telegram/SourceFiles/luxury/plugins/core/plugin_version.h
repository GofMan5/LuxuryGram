// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Luxury::Plugins {

constexpr int kPluginApiVersion = 1;

struct Error {
	std::string text;
};

// Value or an error text that names the offending field.
template <typename T>
class Result {
public:
	Result(T value) : _data(std::move(value)) {
	}
	Result(Error error) : _data(std::move(error)) {
	}

	[[nodiscard]] explicit operator bool() const {
		return _data.index() == 0;
	}
	[[nodiscard]] const T &value() const {
		return std::get<0>(_data);
	}
	[[nodiscard]] T &value() {
		return std::get<0>(_data);
	}
	[[nodiscard]] const T *operator->() const {
		return &std::get<0>(_data);
	}
	[[nodiscard]] const std::string &error() const {
		return std::get<1>(_data).text;
	}

private:
	std::variant<T, Error> _data;

};

// "MAJOR[.MINOR[.PATCH]][-prerelease][+build]", missing parts are 0.
// A prerelease orders below its release; build metadata is ignored.
struct Version {
	int major = 0;
	int minor = 0;
	int patch = 0;
	std::vector<std::string> prerelease;
};

[[nodiscard]] std::optional<Version> ParseVersion(std::string_view text);
[[nodiscard]] std::strong_ordering operator<=>(
	const Version &a,
	const Version &b);
[[nodiscard]] bool operator==(const Version &a, const Version &b);

} // namespace Luxury::Plugins
