// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include "luxury/plugins/core/plugin_manifest.h"
#include "luxury/plugins/core/plugin_registry.h"
#include "luxury/plugins/core/plugin_source.h"
#include "luxury/plugins/plugin_install_logic.h"
#include "base/weak_ptr.h"

#include <QByteArray>
#include <QString>

#include <map>
#include <memory>

class QNetworkAccessManager;

namespace Luxury::Plugins {

// What a link or an update check resolved to. error is human readable,
// code classifies it; subdir is the plugin folder (a tree/ link may move
// its first segments into a branch name with '/'); notice explains a
// fallback the user should know about.
struct Resolution {
	ResolvedRef ref;
	std::string subdir;
	QString notice;
	QString error;
	ErrorCode code = ErrorCode::None;
};

// Best effort: zero / empty when GitHub did not say.
struct Identity {
	std::int64_t repoId = 0;
	std::string commit;
};

// GitHub resolution and downloads, HTTPS only, User-Agent LuxuryGram.
// Callbacks run on the main thread and never after the installer died.
class Installer final : public base::has_weak_ptr {
public:
	Installer();
	~Installer();

	// The ref an install link points at.
	void resolve(const GithubInput &input, Fn<void(Resolution)> done);
	// The newest ref of an installed plugin's channel.
	void latest(const PluginSource &source, Fn<void(Resolution)> done);
	// The head of the default branch.
	void defaultBranch(
		const std::string &owner,
		const std::string &repo,
		Fn<void(Resolution)> done);
	// The repository id and the commit ref points at; never fails.
	void identify(
		const std::string &owner,
		const std::string &repo,
		const ResolvedRef &ref,
		Fn<void(Identity)> done);
	// plugin.json at ref, parsed; raw is what was downloaded.
	void manifest(
		const PluginSource &source,
		Fn<void(
			Manifest manifest,
			QByteArray raw,
			QString error,
			ErrorCode code)> done);
	// Every manifest file into directory (created), at most
	// kMaxPluginBytes in total. Paths were checked by ParseManifest and
	// are checked again here.
	void download(
		const PluginSource &source,
		std::vector<std::string> files,
		QString directory,
		Fn<void(QString error)> done);

private:
	struct DownloadState;

	// error is set with status 0 on a network failure, -1 on an oversized
	// body and the HTTP status on a GitHub rate limit.
	void get(
		const std::string &url,
		qint64 limit,
		Fn<void(int status, QByteArray body, QString error)> done);
	void resolveBranch(
		const std::string &owner,
		const std::string &repo,
		const std::string &branch,
		Fn<void(Resolution)> done);
	// Tries the ref/subdir splits of a tree/ link from index on.
	void resolveSplit(
		const GithubInput &input,
		std::vector<std::pair<std::string, std::string>> splits,
		std::size_t index,
		Fn<void(Resolution)> done);
	void downloadNext(std::shared_ptr<DownloadState> state);

	std::unique_ptr<QNetworkAccessManager> _network;
	// api.github.com answers by URL: ETag and body. A 304 answer to
	// If-None-Match does not count against the rate limit.
	std::map<std::string, std::pair<QByteArray, QByteArray>> _etags;

};

} // namespace Luxury::Plugins
