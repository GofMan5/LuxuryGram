// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/plugin_installer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Luxury::Plugins {
namespace {

constexpr auto kTimeout = 30000;
constexpr auto kApiLimit = qint64(1024) * 1024;

[[nodiscard]] QString Text(const std::string &text) {
	return QString::fromStdString(text);
}

[[nodiscard]] std::string_view View(const QByteArray &body) {
	return std::string_view(body.data(), std::size_t(body.size()));
}

// A non-200 GitHub answer: its "message", otherwise the status.
template <typename Parse>
[[nodiscard]] QString GithubError(
		int status,
		const QByteArray &body,
		Parse parse) {
	const auto parsed = parse(View(body));
	return !parsed
		? Text(parsed.error())
		: (u"GitHub: HTTP "_q + QString::number(status));
}

} // namespace

struct Installer::DownloadState {
	PluginSource source;
	std::vector<std::string> files;
	QString directory;
	Fn<void(QString)> done;
	std::size_t index = 0;
	qint64 total = 0;
};

Installer::Installer()
: _network(std::make_unique<QNetworkAccessManager>()) {
}

Installer::~Installer() {
	// Aborting replies may emit finished while _network is destroyed.
	invalidate_weak_ptrs(this);
}

void Installer::get(
		const std::string &url,
		qint64 limit,
		Fn<void(int, QByteArray, QString)> done) {
	const auto parsed = QUrl(Text(url), QUrl::StrictMode);
	if (url.empty()
		|| !parsed.isValid()
		|| parsed.scheme().toLower() != u"https"_q) {
		done(0, {}, u"Refused a non-HTTPS address."_q);
		return;
	}
	auto request = QNetworkRequest(parsed);
	// raw.githubusercontent.com and api.github.com may redirect (renamed
	// repositories); never to plain HTTP.
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(kTimeout);
	request.setHeader(QNetworkRequest::UserAgentHeader, u"LuxuryGram"_q);
	const auto reply = _network->get(request);
	const auto tooBig = std::make_shared<bool>(false);
	QObject::connect(reply, &QNetworkReply::downloadProgress, [=](
			qint64 received,
			qint64) {
		if (received > limit) {
			*tooBig = true;
			reply->abort();
		}
	});
	const auto weak = base::make_weak(this);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (!weak.get()) {
			return;
		}
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto body = reply->read(limit + 1);
		if (*tooBig || body.size() > limit) {
			done(0, {}, u"The download is too large."_q);
		} else if (status <= 0
			|| reply->url().scheme().toLower() != u"https"_q) {
			done(0, {}, reply->errorString());
		} else {
			done(status, body, QString());
		}
	});
}

void Installer::resolve(
		const GithubInput &input,
		Fn<void(ResolvedRef, QString)> done) {
	const auto owner = input.owner;
	const auto repo = input.repo;
	if (input.kind == GithubKind::Auto) {
		// An empty ref lets latest() fall back to the default branch.
		latest({ .owner = owner, .repo = repo }, std::move(done));
		return;
	}
	const auto ref = input.ref;
	const auto tagOnly = (input.kind == GithubKind::ReleaseTag);
	const auto weak = base::make_weak(this);
	get(ReleaseByTagUrl(owner, repo, ref), kApiLimit, [=](
			int status,
			QByteArray body,
			QString error) {
		if (!status) {
			done({}, error);
		} else if (status == 200) {
			const auto tag = ParseReleaseTag(View(body));
			if (!tag) {
				done({}, Text(tag.error()));
			} else {
				done({ .channel = Channel::Release, .ref = tag.value() }, {});
			}
		} else if (status == 404 && !tagOnly) {
			// tree/<ref> names a branch, or a tag without a release.
			if (const auto strong = weak.get()) {
				strong->resolveBranch(owner, repo, ref, done);
			}
		} else {
			done({}, GithubError(status, body, ParseReleaseTag));
		}
	});
}

void Installer::latest(
		const PluginSource &source,
		Fn<void(ResolvedRef, QString)> done) {
	const auto owner = source.owner;
	const auto repo = source.repo;
	if (source.channel == Channel::Branch) {
		resolveBranch(owner, repo, source.label, std::move(done));
		return;
	}
	const auto fallback = source.ref.empty();
	const auto weak = base::make_weak(this);
	get(LatestReleaseUrl(owner, repo), kApiLimit, [=](
			int status,
			QByteArray body,
			QString error) {
		if (!status) {
			done({}, error);
		} else if (status == 200) {
			const auto tag = ParseReleaseTag(View(body));
			if (!tag) {
				done({}, Text(tag.error()));
			} else {
				done({ .channel = Channel::Release, .ref = tag.value() }, {});
			}
		} else if (status == 404 && fallback) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			strong->get(RepoUrl(owner, repo), kApiLimit, [=](
					int status,
					QByteArray body,
					QString error) {
				if (!status) {
					done({}, error);
					return;
				}
				const auto branch = ParseDefaultBranch(View(body));
				if (status != 200 || !branch) {
					done({}, GithubError(status, body, ParseDefaultBranch));
				} else if (const auto strong = weak.get()) {
					strong->resolveBranch(owner, repo, branch.value(), done);
				}
			});
		} else {
			done({}, GithubError(status, body, ParseReleaseTag));
		}
	});
}

void Installer::resolveBranch(
		const std::string &owner,
		const std::string &repo,
		const std::string &branch,
		Fn<void(ResolvedRef, QString)> done) {
	get(CommitUrl(owner, repo, branch), kApiLimit, [=](
			int status,
			QByteArray body,
			QString error) {
		if (!status) {
			done({}, error);
			return;
		}
		const auto sha = ParseCommitSha(View(body));
		if (status != 200 || !sha) {
			done({}, GithubError(status, body, ParseCommitSha));
		} else {
			done({
				.channel = Channel::Branch,
				.ref = sha.value(),
				.label = branch,
			}, {});
		}
	});
}

void Installer::manifest(
		const PluginSource &source,
		Fn<void(Manifest, QByteArray, QString)> done) {
	const auto url = RawFileUrl(
		source.owner,
		source.repo,
		source.ref,
		source.subdir,
		"plugin.json");
	get(url, qint64(kMaxManifestBytes), [=](
			int status,
			QByteArray body,
			QString error) {
		if (!status) {
			done({}, {}, error);
		} else if (status == 404) {
			done({}, {}, u"plugin.json was not found in the repository."_q);
		} else if (status != 200) {
			done({}, {}, u"plugin.json: HTTP "_q + QString::number(status));
		} else {
			auto parsed = ParseManifest(View(body));
			if (!parsed) {
				done({}, {}, u"plugin.json: "_q + Text(parsed.error()));
			} else {
				done(std::move(parsed.value()), body, {});
			}
		}
	});
}

void Installer::download(
		const PluginSource &source,
		std::vector<std::string> files,
		QString directory,
		Fn<void(QString)> done) {
	if (files.size() > std::size_t(kMaxPluginFiles)) {
		done(u"The plugin has too many files."_q);
		return;
	}
	downloadNext(std::make_shared<DownloadState>(DownloadState{
		.source = source,
		.files = std::move(files),
		.directory = std::move(directory),
		.done = std::move(done),
	}));
}

void Installer::downloadNext(std::shared_ptr<DownloadState> state) {
	if (state->index == state->files.size()) {
		state->done(QString());
		return;
	}
	const auto &path = state->files[state->index];
	const auto url = IsSafeRelativePath(path)
		? RawFileUrl(
			state->source.owner,
			state->source.repo,
			state->source.ref,
			state->source.subdir,
			path)
		: std::string();
	if (url.empty()) {
		state->done(u"Unsafe file path: "_q + Text(path));
		return;
	}
	const auto weak = base::make_weak(this);
	get(url, kMaxPluginBytes - state->total, [=](
			int status,
			QByteArray body,
			QString error) {
		const auto name = Text(state->files[state->index]);
		if (!status) {
			state->done(name + u": "_q + error);
			return;
		} else if (status != 200) {
			state->done(name + u": HTTP "_q + QString::number(status));
			return;
		}
		const auto target = state->directory + '/' + name;
		QDir().mkpath(QFileInfo(target).absolutePath());
		auto file = QFile(target);
		if (!file.open(QIODevice::WriteOnly)
			|| file.write(body) != body.size()) {
			state->done(name + u": "_q + file.errorString());
			return;
		}
		state->total += body.size();
		++state->index;
		if (const auto strong = weak.get()) {
			strong->downloadNext(state);
		}
	});
}

} // namespace Luxury::Plugins
