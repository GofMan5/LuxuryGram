// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/plugin_installer.h"

#include "lang/lang_keys.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Luxury::Plugins {
namespace {

constexpr auto kTimeout = 30000;
constexpr auto kApiLimit = qint64(1024) * 1024;
constexpr auto kApiPrefix = std::string_view("https://api.github.com/");

[[nodiscard]] QString Text(const std::string &text) {
	return QString::fromStdString(text);
}

[[nodiscard]] std::string_view View(const QByteArray &body) {
	return std::string_view(body.data(), std::size_t(body.size()));
}

// The code of a failed get(), see its status convention.
[[nodiscard]] ErrorCode TransportCode(int status) {
	return (status > 0)
		? ErrorCode::RateLimited
		: (status < 0)
		? ErrorCode::TooLarge
		: ErrorCode::Network;
}

[[nodiscard]] Resolution Failed(QString error, ErrorCode code) {
	return { .error = std::move(error), .code = code };
}

// A non-200 GitHub answer.
[[nodiscard]] Resolution Failed(int status, const QByteArray &body) {
	return Failed(
		Text(GithubErrorText(status, View(body))),
		ClassifyStatus(status));
}

[[nodiscard]] Resolution Release(const QByteArray &body) {
	const auto tag = ParseReleaseTag(View(body));
	return tag
		? Resolution{
			.ref = { .channel = Channel::Release, .ref = tag.value() },
		}
		: Failed(Text(tag.error()), ErrorCode::Github);
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
	const auto api = url.starts_with(kApiPrefix);
	if (api) {
		if (const auto i = _etags.find(url); i != end(_etags)) {
			request.setRawHeader("If-None-Match", i->second.first);
		}
	}
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
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto body = reply->read(limit + 1);
		const auto cached = (api && status == 304)
			? strong->_etags.find(url)
			: end(strong->_etags);
		const auto header = [&](const char *name) {
			return reply->rawHeader(name);
		};
		const auto retryAfter = header("Retry-After");
		if (*tooBig || body.size() > limit) {
			done(-1, {}, u"The download is too large."_q);
		} else if (status <= 0
			|| reply->url().scheme().toLower() != u"https"_q) {
			done(0, {}, reply->errorString());
		} else if (cached != end(strong->_etags)) {
			const auto copy = cached->second.second;
			done(200, copy, QString());
		} else if (IsRateLimited(
				status,
				View(header("X-RateLimit-Remaining")),
				View(retryAfter))) {
			const auto at = RateLimitResetAt(
				View(header("X-RateLimit-Reset")),
				View(retryAfter),
				QDateTime::currentSecsSinceEpoch());
			const auto time = QLocale().toString(
				QDateTime::fromSecsSinceEpoch(at).time(),
				QLocale::ShortFormat);
			done(
				status,
				body,
				tr::lng_luxury_plugins_rate_limited(tr::now, lt_time, time));
		} else {
			const auto etag = header("ETag");
			if (api && status == 200 && !etag.isEmpty()) {
				strong->_etags[url] = { etag, body };
			}
			done(status, body, QString());
		}
	});
}

void Installer::resolve(
		const GithubInput &input,
		Fn<void(Resolution)> done) {
	if (input.kind == GithubKind::Auto) {
		// An empty ref lets latest() fall back to the default branch.
		const auto subdir = input.subdir;
		latest({ .owner = input.owner, .repo = input.repo }, [=](
				Resolution result) {
			result.subdir = subdir;
			done(std::move(result));
		});
		return;
	}
	const auto splits = (input.kind == GithubKind::ReleaseTag)
		? std::vector{ std::pair{ input.ref, input.subdir } }
		: RefSplits(input.ref, input.subdir);
	resolveSplit(input, splits, 0, std::move(done));
}

void Installer::resolveSplit(
		const GithubInput &input,
		std::vector<std::pair<std::string, std::string>> splits,
		std::size_t index,
		Fn<void(Resolution)> done) {
	const auto owner = input.owner;
	const auto repo = input.repo;
	const auto ref = splits[index].first;
	const auto subdir = splits[index].second;
	const auto tagOnly = (input.kind == GithubKind::ReleaseTag);
	const auto weak = base::make_weak(this);
	get(ReleaseByTagUrl(owner, repo, ref), kApiLimit, [=](
			int status,
			QByteArray body,
			QString error) {
		if (!error.isEmpty()) {
			done(Failed(error, TransportCode(status)));
		} else if (status == 200) {
			auto result = Release(body);
			result.subdir = subdir;
			done(std::move(result));
		} else if (status == 404 && !tagOnly) {
			// tree/<ref> names a branch, or a tag without a release.
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			strong->resolveBranch(owner, repo, ref, [=](Resolution result) {
				const auto next = index + 1;
				if (result.code == ErrorCode::NotFound
					&& next < splits.size()) {
					// tree/feature/x/plugin: the branch may be "feature/x".
					if (const auto alive = weak.get()) {
						alive->resolveSplit(input, splits, next, done);
					}
					return;
				}
				result.subdir = subdir;
				done(std::move(result));
			});
		} else {
			done(Failed(status, body));
		}
	});
}

void Installer::latest(
		const PluginSource &source,
		Fn<void(Resolution)> done) {
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
		if (!error.isEmpty()) {
			done(Failed(error, TransportCode(status)));
		} else if (status == 200) {
			done(Release(body));
		} else if (status == 404 && fallback) {
			if (const auto strong = weak.get()) {
				strong->defaultBranch(owner, repo, done);
			}
		} else {
			done(Failed(status, body));
		}
	});
}

void Installer::defaultBranch(
		const std::string &owner,
		const std::string &repo,
		Fn<void(Resolution)> done) {
	const auto weak = base::make_weak(this);
	get(RepoUrl(owner, repo), kApiLimit, [=](
			int status,
			QByteArray body,
			QString error) {
		if (!error.isEmpty()) {
			done(Failed(error, TransportCode(status)));
		} else if (status != 200) {
			done(Failed(status, body));
		} else if (const auto branch = ParseDefaultBranch(View(body)); !branch) {
			done(Failed(Text(branch.error()), ErrorCode::Github));
		} else if (const auto strong = weak.get()) {
			strong->resolveBranch(owner, repo, branch.value(), done);
		}
	});
}

void Installer::resolveBranch(
		const std::string &owner,
		const std::string &repo,
		const std::string &branch,
		Fn<void(Resolution)> done) {
	get(CommitUrl(owner, repo, branch), kApiLimit, [=](
			int status,
			QByteArray body,
			QString error) {
		if (!error.isEmpty()) {
			done(Failed(error, TransportCode(status)));
		} else if (status != 200) {
			done(Failed(status, body));
		} else if (const auto sha = ParseCommitSha(View(body)); !sha) {
			done(Failed(Text(sha.error()), ErrorCode::Github));
		} else {
			done({ .ref = {
				.channel = Channel::Branch,
				.ref = sha.value(),
				.label = branch,
			} });
		}
	});
}

void Installer::identify(
		const std::string &owner,
		const std::string &repo,
		const ResolvedRef &ref,
		Fn<void(Identity)> done) {
	const auto weak = base::make_weak(this);
	get(RepoUrl(owner, repo), kApiLimit, [=](
			int status,
			QByteArray body,
			QString error) {
		auto result = Identity();
		if (error.isEmpty() && status == 200) {
			result.repoId = ParseRepoId(View(body)).value_or(0);
		}
		if (ref.channel == Channel::Branch && IsCommitSha(ref.ref)) {
			result.commit = ref.ref;
			done(std::move(result));
			return;
		}
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		strong->get(CommitUrl(owner, repo, ref.ref), kApiLimit, [=](
				int status,
				QByteArray body,
				QString error) {
			auto copy = result;
			if (error.isEmpty() && status == 200) {
				const auto sha = ParseCommitSha(View(body));
				if (sha) {
					copy.commit = sha.value();
				}
			}
			done(std::move(copy));
		});
	});
}

void Installer::manifest(
		const PluginSource &source,
		Fn<void(Manifest, QByteArray, QString, ErrorCode)> done) {
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
		if (!error.isEmpty()) {
			done({}, {}, error, TransportCode(status));
		} else if (status == 404) {
			done(
				{},
				{},
				u"plugin.json was not found in the repository."_q,
				ErrorCode::NotFound);
		} else if (status != 200) {
			done(
				{},
				{},
				u"plugin.json: HTTP "_q + QString::number(status),
				ErrorCode::Github);
		} else {
			auto parsed = ParseManifest(View(body));
			if (!parsed) {
				done(
					{},
					{},
					u"plugin.json: "_q + Text(parsed.error()),
					ErrorCode::Manifest);
			} else {
				done(std::move(parsed.value()), body, {}, ErrorCode::None);
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
		if (!error.isEmpty()) {
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
