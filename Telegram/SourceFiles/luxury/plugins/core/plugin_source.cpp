// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/core/plugin_source.h"

#include "luxury/libs/json.hpp"
#include "luxury/plugins/core/plugin_manifest.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace Luxury::Plugins {
namespace {

using Json = nlohmann::json;

constexpr auto kMaxGithubName = std::size_t(100);
constexpr auto kMaxRefLength = std::size_t(255);
constexpr auto kShaLength = std::size_t(40);
constexpr auto kApi = std::string_view("https://api.github.com/repos/");
constexpr auto kRaw = std::string_view("https://raw.githubusercontent.com/");

[[nodiscard]] Error Fail(std::string_view problem) {
	return Error{ "link: " + std::string(problem) };
}

[[nodiscard]] char Lower(char c) {
	return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

[[nodiscard]] bool IsAlnum(char c) {
	return (c >= 'a' && c <= 'z')
		|| (c >= 'A' && c <= 'Z')
		|| (c >= '0' && c <= '9');
}

[[nodiscard]] bool StartsWithNoCase(
		std::string_view text,
		std::string_view prefix) {
	return text.size() >= prefix.size()
		&& std::equal(prefix.begin(), prefix.end(), text.begin(), [](
				char a,
				char b) {
			return Lower(a) == Lower(b);
		});
}

[[nodiscard]] int HexValue(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	} else if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	} else if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

[[nodiscard]] std::optional<std::string> PercentDecode(std::string_view text) {
	auto result = std::string();
	for (auto i = std::size_t(); i != text.size(); ++i) {
		if (text[i] != '%') {
			result += text[i];
			continue;
		} else if (i + 2 >= text.size()) {
			return std::nullopt;
		}
		const auto high = HexValue(text[i + 1]);
		const auto low = HexValue(text[i + 2]);
		if (high < 0 || low < 0) {
			return std::nullopt;
		}
		result += char(high * 16 + low);
		i += 2;
	}
	return result;
}

[[nodiscard]] std::string PercentEncode(std::string_view segment) {
	static constexpr auto kHex = std::string_view("0123456789ABCDEF");
	auto result = std::string();
	for (const auto c : segment) {
		if (IsAlnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
			result += c;
		} else {
			const auto byte = static_cast<unsigned char>(c);
			result += '%';
			result += kHex[byte >> 4];
			result += kHex[byte & 0x0F];
		}
	}
	return result;
}

// Keeps '/' between segments.
[[nodiscard]] std::string EncodePath(std::string_view path) {
	auto result = std::string();
	while (true) {
		const auto slash = path.find('/');
		result += PercentEncode(path.substr(0, slash));
		if (slash == std::string_view::npos) {
			return result;
		}
		result += '/';
		path = path.substr(slash + 1);
	}
}

[[nodiscard]] std::vector<std::string_view> Split(std::string_view path) {
	auto result = std::vector<std::string_view>();
	while (true) {
		const auto slash = path.find('/');
		result.push_back(path.substr(0, slash));
		if (slash == std::string_view::npos) {
			return result;
		}
		path = path.substr(slash + 1);
	}
}

[[nodiscard]] std::string_view Trim(std::string_view text) {
	const auto space = [](char c) {
		return c == ' ' || c == '\t' || c == '\r' || c == '\n';
	};
	while (!text.empty() && space(text.front())) {
		text.remove_prefix(1);
	}
	while (!text.empty() && space(text.back())) {
		text.remove_suffix(1);
	}
	return text;
}

// Strips scheme and host, returns the path after "github.com/", or the
// whole input when it has neither (the "owner/repo" form).
[[nodiscard]] std::optional<std::string_view> GithubPath(
		std::string_view input) {
	auto hadScheme = false;
	for (const auto scheme : { "https://", "http://" }) {
		if (StartsWithNoCase(input, scheme)) {
			input.remove_prefix(std::string_view(scheme).size());
			hadScheme = true;
			break;
		}
	}
	if (!hadScheme && StartsWithNoCase(input, "git@github.com:")) {
		return input.substr(15);
	} else if (StartsWithNoCase(input, "www.")) {
		input.remove_prefix(4);
	}
	if (StartsWithNoCase(input, "github.com/")) {
		return input.substr(11);
	} else if (hadScheme || StartsWithNoCase(input, "github.com")) {
		return std::nullopt;
	}
	return input;
}

[[nodiscard]] Result<std::string> JsonField(
		std::string_view json,
		const char *key) {
	const auto document = Json::parse(json, nullptr, false);
	if (document.is_discarded() || !document.is_object()) {
		return Error{ "GitHub: response is not a JSON object" };
	}
	const auto value = document.find(key);
	if (value != document.end() && value->is_string()) {
		return value->get<std::string>();
	}
	const auto message = document.find("message");
	if (message != document.end() && message->is_string()) {
		return Error{ "GitHub: " + message->get<std::string>() };
	}
	return Error{ std::string("GitHub: ") + key + " is missing" };
}

[[nodiscard]] std::string ApiBase(
		std::string_view owner,
		std::string_view repo) {
	return std::string(kApi)
		+ PercentEncode(owner)
		+ '/'
		+ PercentEncode(repo);
}

} // namespace

bool IsValidGithubName(std::string_view name) {
	return !name.empty()
		&& name.size() <= kMaxGithubName
		&& name != "."
		&& name != ".."
		&& std::all_of(name.begin(), name.end(), [](char c) {
			return IsAlnum(c) || c == '_' || c == '.' || c == '-';
		});
}

bool IsValidGitRef(std::string_view ref) {
	return !ref.empty()
		&& ref.size() <= kMaxRefLength
		&& ref.front() != '-'
		&& ref.front() != '.'
		&& ref.front() != '/'
		&& ref.back() != '/'
		&& ref.back() != '.'
		&& !ref.ends_with(".lock")
		&& ref.find("..") == std::string_view::npos
		&& ref.find("//") == std::string_view::npos
		&& ref.find("/.") == std::string_view::npos
		&& std::all_of(ref.begin(), ref.end(), [](char c) {
			return IsAlnum(c)
				|| c == '_'
				|| c == '.'
				|| c == '+'
				|| c == '/'
				|| c == '-';
		});
}

Result<GithubInput> ParseGithubInput(std::string_view input) {
	input = Trim(input);
	input = input.substr(0, input.find_first_of("?#"));
	const auto path = GithubPath(input);
	if (!path) {
		return Fail("only github.com links are supported");
	}
	auto segments = Split(*path);
	if (segments.size() > 2 && segments.back().empty()) {
		segments.pop_back();
	}
	if (segments.size() < 2) {
		return Fail("expected github.com/owner/repo");
	}
	auto result = GithubInput();
	result.owner = std::string(segments[0]);
	result.repo = std::string(segments[1]);
	if (result.repo.ends_with(".git")) {
		result.repo.resize(result.repo.size() - 4);
	}
	if (!IsValidGithubName(result.owner)) {
		return Fail("owner must be [A-Za-z0-9_.-]");
	} else if (!IsValidGithubName(result.repo)) {
		return Fail("repository must be [A-Za-z0-9_.-]");
	} else if (segments.size() == 2) {
		return result;
	}
	const auto decode = [&](std::size_t from) -> std::optional<std::string> {
		auto joined = std::string();
		for (auto i = from; i != segments.size(); ++i) {
			if (segments[i].empty()) {
				return std::nullopt;
			}
			const auto decoded = PercentDecode(segments[i]);
			if (!decoded) {
				return std::nullopt;
			}
			joined += (i == from ? "" : "/") + *decoded;
		}
		return joined;
	};
	const auto blob = (segments[2] == "blob" && segments.size() >= 5);
	if (blob) {
		segments.pop_back(); // A file: its folder is the plugin.
	}
	if ((segments[2] == "tree" || blob) && segments.size() >= 4) {
		const auto ref = PercentDecode(segments[3]);
		if (!ref || !IsValidGitRef(*ref)) {
			return Fail("bad branch or tag name");
		}
		result.kind = GithubKind::Ref;
		result.ref = *ref;
		if (segments.size() > 4) {
			const auto subdir = decode(4);
			if (!subdir || !IsSafeRelativePath(*subdir)) {
				return Fail("bad folder path");
			}
			result.subdir = *subdir;
		}
		return result;
	} else if (segments[2] == "releases"
		&& segments.size() >= 5
		&& segments[3] == "tag") {
		const auto tag = decode(4);
		if (!tag || !IsValidGitRef(*tag)) {
			return Fail("bad release tag");
		}
		result.kind = GithubKind::ReleaseTag;
		result.ref = *tag;
		return result;
	} else if (segments[2] == "releases"
		&& segments.size() == 4
		&& segments[3] == "latest") {
		return result;
	} else if (segments[2] == "commit" && segments.size() == 4) {
		const auto sha = segments[3];
		const auto hex = std::all_of(sha.begin(), sha.end(), [](char c) {
			return HexValue(c) >= 0;
		});
		if (sha.size() < 7 || sha.size() > kShaLength || !hex) {
			return Fail("bad commit");
		}
		result.kind = GithubKind::Ref;
		result.ref = std::string(sha);
		return result;
	}
	return Fail("expected a repository, tree/<ref>, commit/<sha> or "
		"releases/tag/<tag> link");
}

std::string LatestReleaseUrl(std::string_view owner, std::string_view repo) {
	return ApiBase(owner, repo) + "/releases/latest";
}

std::string RepoUrl(std::string_view owner, std::string_view repo) {
	return ApiBase(owner, repo);
}

std::string CommitUrl(
		std::string_view owner,
		std::string_view repo,
		std::string_view branch) {
	return ApiBase(owner, repo) + "/commits/" + PercentEncode(branch);
}

std::string ReleaseByTagUrl(
		std::string_view owner,
		std::string_view repo,
		std::string_view tag) {
	return ApiBase(owner, repo) + "/releases/tags/" + PercentEncode(tag);
}

std::string RawFileUrl(
		std::string_view owner,
		std::string_view repo,
		std::string_view ref,
		std::string_view subdir,
		std::string_view path) {
	if (!IsValidGithubName(owner)
		|| !IsValidGithubName(repo)
		|| !IsValidGitRef(ref)
		|| (!subdir.empty() && !IsSafeRelativePath(subdir))
		|| !IsSafeRelativePath(path)) {
		return std::string();
	}
	auto result = std::string(kRaw)
		+ std::string(owner)
		+ '/'
		+ std::string(repo)
		+ '/'
		+ EncodePath(ref)
		+ '/';
	if (!subdir.empty()) {
		result += EncodePath(subdir) + '/';
	}
	return result + EncodePath(path);
}

Result<std::string> ParseReleaseTag(std::string_view json) {
	auto tag = JsonField(json, "tag_name");
	if (tag && !IsValidGitRef(tag.value())) {
		return Error{ "GitHub: release tag \"" + tag.value() + "\" is not supported" };
	}
	return tag;
}

Result<std::string> ParseDefaultBranch(std::string_view json) {
	auto branch = JsonField(json, "default_branch");
	if (branch && !IsValidGitRef(branch.value())) {
		return Error{ "GitHub: branch \"" + branch.value() + "\" is not supported" };
	}
	return branch;
}

Result<std::string> ParseCommitSha(std::string_view json) {
	auto sha = JsonField(json, "sha");
	if (!sha) {
		return sha;
	}
	auto &value = sha.value();
	std::transform(value.begin(), value.end(), value.begin(), Lower);
	if (!IsCommitSha(value)) {
		return Error{ "GitHub: commit sha is not 40 hex digits" };
	}
	return sha;
}

bool IsCommitSha(std::string_view sha) {
	return (sha.size() == kShaLength)
		&& std::all_of(sha.begin(), sha.end(), [](char c) {
			return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
		});
}

UpdateDecision DecideUpdate(
		const ResolvedRef &stored,
		const ResolvedRef &latest) {
	if (latest.ref.empty()
		|| (stored.channel == latest.channel && stored.ref == latest.ref)) {
		return UpdateDecision::UpToDate;
	} else if (stored.channel == Channel::Release
		&& latest.channel == Channel::Release) {
		const auto version = [](std::string_view tag) {
			if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V')) {
				tag.remove_prefix(1);
			}
			return ParseVersion(tag);
		};
		const auto was = version(stored.ref);
		const auto now = version(latest.ref);
		if (was && now && *now < *was) {
			return UpdateDecision::Downgrade;
		}
	}
	return UpdateDecision::Available;
}

} // namespace Luxury::Plugins
