// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include "luxury/plugins/core/plugin_version.h"

#include <string>
#include <string_view>

namespace Luxury::Plugins {

enum class GithubKind {
	Auto, // Latest release, otherwise the default branch.
	Ref, // tree/<ref>[/<subdir>]: a branch, a tag or a commit.
	ReleaseTag, // releases/tag/<tag>.
};

struct GithubInput {
	std::string owner;
	std::string repo;
	GithubKind kind = GithubKind::Auto;
	std::string ref;
	std::string subdir; // Empty or a safe relative path, no trailing '/'.
};

enum class Channel {
	Release,
	Branch,
};

// What an install or update check resolved to: a release tag, or a commit
// SHA of the branch named by label.
struct ResolvedRef {
	Channel channel = Channel::Release;
	std::string ref;
	std::string label;
};

enum class UpdateDecision {
	UpToDate,
	Available,
	Downgrade, // An older release than the installed one.
};

// [A-Za-z0-9_.-]{1,100}, not "." or "..".
[[nodiscard]] bool IsValidGithubName(std::string_view name);

// A tag or branch name: [A-Za-z0-9_.+/-], no "..", "//", "@{", no leading
// '-', '.' or '/', no trailing '/', '.' or ".lock".
[[nodiscard]] bool IsValidGitRef(std::string_view ref);

// Accepts https://github.com/o/r, github.com/o/r, o/r, git@github.com:o/r
// (optional scheme, "www.", ".git", trailing slash) and these pages:
// - .../tree/<ref>[/<subdir>], the first segment is the ref; a branch with
//   '/' in its name is retried by the installer, see RefSplits;
// - .../blob/<ref>[/<subdir>]/<file>, the file's folder, as tree/;
// - .../commit/<sha>, a Ref;
// - .../releases/tag/<tag>, every segment after tag/ is the tag;
// - .../releases and .../releases/latest, the same as the repository.
// Query and fragment are dropped, path segments are percent-decoded before
// validation.
[[nodiscard]] Result<GithubInput> ParseGithubInput(std::string_view input);

[[nodiscard]] std::string LatestReleaseUrl(
	std::string_view owner,
	std::string_view repo);
[[nodiscard]] std::string RepoUrl(
	std::string_view owner,
	std::string_view repo);
[[nodiscard]] std::string CommitUrl(
	std::string_view owner,
	std::string_view repo,
	std::string_view branch);
[[nodiscard]] std::string ReleaseByTagUrl(
	std::string_view owner,
	std::string_view repo,
	std::string_view tag);

// Empty when ref, subdir or path is not valid.
[[nodiscard]] std::string RawFileUrl(
	std::string_view owner,
	std::string_view repo,
	std::string_view ref,
	std::string_view subdir,
	std::string_view path);

// Bodies of the api.github.com responses. A body without the field, such
// as {"message": "API rate limit exceeded ..."}, yields that message.
[[nodiscard]] Result<std::string> ParseReleaseTag(std::string_view json);
[[nodiscard]] Result<std::string> ParseDefaultBranch(std::string_view json);
[[nodiscard]] Result<std::string> ParseCommitSha(std::string_view json);

// 40 lowercase hex digits.
[[nodiscard]] bool IsCommitSha(std::string_view sha);

// Available when the tag or SHA changed; an empty latest ref is never
// offered as an update. Downgrade when both are releases whose tags
// ("v1.2.0" or "1.2.0") parse as versions and latest is the older one.
[[nodiscard]] UpdateDecision DecideUpdate(
	const ResolvedRef &stored,
	const ResolvedRef &latest);

} // namespace Luxury::Plugins
