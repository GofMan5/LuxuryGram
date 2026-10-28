// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include "luxury/plugins/core/plugin_manifest.h"
#include "luxury/plugins/core/plugin_registry.h"
#include "luxury/plugins/core/plugin_source.h"
#include "base/weak_ptr.h"

#include <QString>

#include <memory>

class QNetworkAccessManager;

namespace Luxury::Plugins {

// GitHub resolution and downloads, HTTPS only, User-Agent LuxuryGram.
// Callbacks run on the main thread and never after the installer died.
class Installer final : public base::has_weak_ptr {
public:
	Installer();
	~Installer();

	// The ref an install link points at; error is human readable.
	void resolve(
		const GithubInput &input,
		Fn<void(ResolvedRef ref, QString error)> done);
	// The newest ref of an installed plugin's channel.
	void latest(
		const PluginSource &source,
		Fn<void(ResolvedRef ref, QString error)> done);
	// plugin.json at ref, parsed; raw is what was downloaded.
	void manifest(
		const PluginSource &source,
		Fn<void(Manifest manifest, QByteArray raw, QString error)> done);
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

	// status is 0 with error on a network failure or an oversized body.
	void get(
		const std::string &url,
		qint64 limit,
		Fn<void(int status, QByteArray body, QString error)> done);
	void resolveBranch(
		const std::string &owner,
		const std::string &repo,
		const std::string &branch,
		Fn<void(ResolvedRef ref, QString error)> done);
	void downloadNext(std::shared_ptr<DownloadState> state);

	std::unique_ptr<QNetworkAccessManager> _network;

};

} // namespace Luxury::Plugins
