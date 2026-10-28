// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/plugin_manager.h"

#include "luxury/plugins/core/plugin_registry.h"
#include "luxury/plugins/plugin_host_api.h"
#include "luxury/plugins/plugin_host_logic.h"
#include "luxury/plugins/plugin_installer.h"
#include "base/call_delayed.h"
#include "core/version.h"
#include "lang/lang_keys.h"
#include "logs.h"
#include "settings.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QUuid>
#include <QtGui/QGuiApplication>

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <utility>

namespace Luxury::Plugins {
namespace {

using json = nlohmann::json;

constexpr auto kChangesDelay = crl::time(250);

Manager *Current = nullptr;

[[nodiscard]] QString Root() {
	return cWorkingDir() + u"tdata/luxury_plugins/"_q;
}

// Under plugins/ so no id can collide with registry.json, .staging or
// any other file the manager keeps in Root().
[[nodiscard]] QString PluginRoot(const QString &id) {
	return Root() + u"plugins/"_q + id;
}

[[nodiscard]] QString Text(const std::string &text) {
	return QString::fromStdString(text);
}

// A char8_t string is UTF-8 on every platform; wchar_t and char are not.
[[nodiscard]] std::filesystem::path FsPath(const QString &path) {
	const auto utf8 = QDir::toNativeSeparators(path).toStdString();
	return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

[[nodiscard]] std::optional<json> ReadJson(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	auto result = json::parse(file.readAll().toStdString(), nullptr, false);
	return result.is_discarded()
		? std::nullopt
		: std::make_optional(std::move(result));
}

[[nodiscard]] bool WriteFile(const QString &path, const QByteArray &data) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QSaveFile(path);
	return file.open(QIODevice::WriteOnly)
		&& (file.write(data) == data.size())
		&& file.commit();
}

[[nodiscard]] InstallPlan Failed(ErrorCode code) {
	auto result = InstallPlan();
	result.errorCode = code;
	return result;
}

[[nodiscard]] ResolvedRef StoredRef(const PluginSource &source) {
	return {
		.channel = source.channel,
		.ref = source.ref,
		.label = source.label,
	};
}

} // namespace

struct Manager::Private final
	: public InstanceDelegate
	, public base::has_weak_ptr {
	Registry registry;
	std::map<QString, Manifest> manifests;
	std::map<QString, std::unique_ptr<Plugins::Instance>> running;
	std::map<QString, std::deque<LogLine>> logs;
	std::map<QString, json> settings;
	std::map<QString, QString> updates; // id -> availableLabel.
	std::set<QString> committing;
	std::map<QString, LogBudget> logBudgets;
	StartFence fence;
	// Callers of a check that is running already wait for its result.
	std::vector<Fn<void(UpdateCheck)>> checkWaiters;
	Installer installer;
	rpl::event_stream<> changes;
	bool changesQueued = false;

	[[nodiscard]] InstalledPlugin *find(const QString &id);
	// The plugin installed from owner/repo/subdir, names compared as GitHub
	// does: case-insensitive.
	[[nodiscard]] InstalledPlugin *installedFrom(
		const std::string &owner,
		const std::string &repo,
		const std::string &subdir);
	[[nodiscard]] const Manifest *manifest(const QString &id);
	void load();
	// Finishes or rolls back the swaps a killed launch left and disables
	// the plugins it died starting. True when the registry changed.
	[[nodiscard]] bool recover();
	// prepareInstall and prepareUpdate once the ref is known. updating is
	// the id prepareUpdate checks for; fallback allows one retry from the
	// default branch when the latest release has no plugin.json.
	void buildPlan(
		GithubInput input,
		Resolution resolution,
		QString updating,
		bool fallback,
		Fn<void(InstallPlan, QString)> done);
	bool save(); // False when registry.json could not be written.
	void append(const QString &id, Js::LogLevel level, const QString &text);
	void setError(const QString &id, const QString &text);
	void fireChangesLater();
	void start(const QString &id, const char *reason);
	// One plugin per event loop turn, so each start's sentinel is cleared
	// before the next one is written.
	void startNext(std::deque<QString> queue);
	void stop(const QString &id);
	[[nodiscard]] json &settingsOf(const QString &id);

	void instanceLog(
		const QString &id,
		Js::LogLevel level,
		const QString &text) override;
	void instanceError(const QString &id, const QString &text) override;
	void instanceStop(const QString &id, const QString &text) override;
	json instanceSetting(const QString &id, const QString &key) override;
};

InstalledPlugin *Manager::Private::find(const QString &id) {
	const auto key = id.toStdString();
	for (auto &plugin : registry.plugins) {
		if (plugin.id == key) {
			return &plugin;
		}
	}
	return nullptr;
}

InstalledPlugin *Manager::Private::installedFrom(
		const std::string &owner,
		const std::string &repo,
		const std::string &subdir) {
	for (auto &plugin : registry.plugins) {
		if (SameRepo(owner, repo, plugin.source.owner, plugin.source.repo)
			&& subdir == plugin.source.subdir) {
			return &plugin;
		}
	}
	return nullptr;
}

const Manifest *Manager::Private::manifest(const QString &id) {
	if (const auto i = manifests.find(id); i != manifests.end()) {
		return &i->second;
	}
	auto file = QFile(PluginRoot(id) + u"/code/plugin.json"_q);
	if (file.size() > qint64(kMaxManifestBytes)
		|| !file.open(QIODevice::ReadOnly)) {
		return nullptr;
	}
	auto parsed = ParseManifest(file.readAll().toStdString());
	if (!parsed || Text(parsed->id) != id) {
		return nullptr;
	}
	return &manifests.emplace(id, std::move(parsed.value())).first->second;
}

void Manager::Private::load() {
	QDir(Root() + u".staging"_q).removeRecursively();
	const auto path = Root() + u"registry.json"_q;
	const auto backup = path + u".bak"_q;
	const auto read = [](const QString &name) -> std::optional<Registry> {
		auto file = QFile(name);
		if (!file.open(QIODevice::ReadOnly)) {
			LOG(("Luxury Plugins: could not read %1."
				).arg(QFileInfo(name).fileName()));
			return std::nullopt;
		}
		auto parsed = ParseRegistry(file.readAll().toStdString());
		if (!parsed) {
			LOG(("Luxury Plugins: %1 is broken: %2"
				).arg(QFileInfo(name).fileName(), Text(parsed.error())));
			return std::nullopt;
		}
		return std::move(parsed.value());
	};
	const auto exists = QFile::exists(path);
	auto primary = exists ? read(path) : std::nullopt;
	auto restored = (exists && !primary && QFile::exists(backup))
		? read(backup)
		: std::nullopt;
	auto changed = false;
	switch (ChooseRegistry(exists, primary.has_value(), restored.has_value())) {
	case RegistryChoice::Main:
		registry = std::move(*primary);
		break;
	case RegistryChoice::Backup:
		LOG(("Luxury Plugins: registry.json restored from registry.json.bak."));
		registry = std::move(*restored);
		changed = true;
		break;
	case RegistryChoice::Empty:
		break;
	}
	if (exists && !primary) {
		// Keep the broken file for inspection.
		QFile::remove(path + u".bad"_q);
		QFile::rename(path, path + u".bad"_q);
	}
	for (const auto &skipped : registry.skipped) {
		LOG(("Luxury Plugins: registry entry skipped, %1"
			).arg(Text(skipped)));
	}
	if (recover() || changed) {
		save();
	}
}

bool Manager::Private::recover() {
	auto changed = false;
	const auto ids = QDir(Root() + u"plugins"_q).entryList(
		QDir::Dirs | QDir::NoDotAndDotDot);
	for (const auto &id : ids) {
		const auto plugin = find(id);
		const auto code = PluginRoot(id) + u"/code"_q;
		const auto recovery = RecoverSwap(
			FsPath(code),
			plugin ? plugin->source.ref : std::string());
		if (recovery == SwapRecovery::Finished) {
			LOG(("Luxury Plugins: finished the install of %1.").arg(id));
		} else if (recovery == SwapRecovery::RolledBack) {
			LOG(("Luxury Plugins: rolled back the install of %1.").arg(id));
		}
		if (!plugin) {
			continue;
		} else if (!plugin->previousVersion.empty()
			&& !QFileInfo::exists(code + u".prev/plugin.json"_q)) {
			plugin->previousVersion.clear();
			plugin->previousSource = PluginSource();
			changed = true;
		}
		if (TakeStarting(FsPath(PluginRoot(id))) && plugin->enabled) {
			// The last launch died starting it: it must not take this one.
			LOG(("Luxury Plugins: %1 was disabled, the client closed "
				"unexpectedly while it was starting.").arg(id));
			plugin->enabled = false;
			plugin->lastError = std::string(kQuarantinedError);
			changed = true;
		}
	}
	return changed;
}

bool Manager::Private::save() {
	auto copy = registry;
	copy.skipped.clear();
	const auto path = Root() + u"registry.json"_q;
	const auto backup = path + u".bak"_q;
	// load() falls back to the last registry that was written whole.
	if (QFile::exists(path)) {
		QFile::remove(backup);
		QFile::copy(path, backup);
	}
	if (!WriteFile(
			path,
			QByteArray::fromStdString(SerializeRegistry(copy)))) {
		LOG(("Luxury Plugins: could not write registry.json."));
		return false;
	}
	return true;
}

void Manager::Private::append(
		const QString &id,
		Js::LogLevel level,
		const QString &text) {
	const auto now = QDateTime::currentMSecsSinceEpoch();
	auto &lines = logs[id];
	lines.push_back({
		.time = now,
		.level = int(level),
		.text = Text(TruncateText(text.toStdString())),
	});
	while (lines.size() > std::size_t(kLogLimit)) {
		lines.pop_front();
	}
	// Into log.txt too, so a bug report carries it; a chatty plugin is cut.
	auto &budget = logBudgets[id];
	if (budget.allow(now)) {
		const auto tag = (level == Js::LogLevel::Error)
			? u"error"_q
			: (level == Js::LogLevel::Warn)
			? u"warn"_q
			: u"info"_q;
		LOG(("Plugin %1 [%2]: %3").arg(
			id,
			tag,
			Text(OneLine(lines.back().text.toStdString()))));
	} else if (budget.count == kLogMirrorLines + 1) {
		LOG(("Plugin %1: too many log lines, the rest of this minute "
			"stays in the plugin log.").arg(id));
	}
}

// A plugin may throw on every timer tick: lastError is not written to disk
// here (stop, setEnabled and commits save it) and the UI hears of it at
// most every kChangesDelay.
void Manager::Private::setError(const QString &id, const QString &text) {
	append(id, Js::LogLevel::Error, text);
	const auto plugin = find(id);
	const auto error = TruncateText(text.toStdString());
	if (plugin && plugin->lastError != error) {
		plugin->lastError = error;
		fireChangesLater();
	}
}

void Manager::Private::fireChangesLater() {
	if (changesQueued) {
		return;
	}
	changesQueued = true;
	base::call_delayed(kChangesDelay, this, [=] {
		changesQueued = false;
		changes.fire({});
	});
}

void Manager::Private::start(const QString &id, const char *reason) {
	const auto plugin = find(id);
	if (cNoPlugins()
		|| !plugin
		|| !plugin->enabled
		|| running.contains(id)) {
		return;
	}
	const auto parsed = manifest(id);
	if (!parsed) {
		setError(id, u"code/plugin.json is missing or broken."_q);
		return;
	} else if (const auto error = CompatibilityError(
			*parsed,
			LuxuryVersionStr); !error.empty()) {
		setError(id, Text(error));
		return;
	}
	plugin->loadReason = reason;
	auto instance = std::make_unique<Plugins::Instance>(
		*plugin,
		*parsed,
		PluginRoot(id),
		this);
	const auto raw = instance.get();
	running.emplace(id, std::move(instance));
	// Removed on the first event loop turn after the start; found by the
	// next launch it means the start took the client down.
	const auto directory = FsPath(PluginRoot(id));
	fence.mark(directory);
	if (!raw->start()) {
		// The error is in the log and lastError already.
		running.erase(id);
		fence.clear(directory);
		return;
	}
	crl::on_main(this, [=] {
		fence.clear(directory);
	});
}

void Manager::Private::startNext(std::deque<QString> queue) {
	if (queue.empty()) {
		changes.fire({});
		return;
	}
	start(queue.front(), "startup");
	queue.pop_front();
	crl::on_main(this, [=] {
		startNext(queue);
	});
}

void Manager::Private::stop(const QString &id) {
	const auto i = running.find(id);
	if (i == running.end()) {
		return;
	}
	fence.clear(FsPath(PluginRoot(id)));
	auto instance = std::move(i->second);
	running.erase(i);
	instance->unload();
}

json &Manager::Private::settingsOf(const QString &id) {
	const auto i = settings.find(id);
	if (i != settings.end()) {
		return i->second;
	}
	auto read = ReadJson(PluginRoot(id) + u"/data/settings.json"_q);
	return settings.emplace(
		id,
		(read && read->is_object()) ? std::move(*read) : json::object()
	).first->second;
}

void Manager::Private::instanceLog(
		const QString &id,
		Js::LogLevel level,
		const QString &text) {
	append(id, level, text);
}

void Manager::Private::instanceError(
		const QString &id,
		const QString &text) {
	setError(id, text);
}

void Manager::Private::instanceStop(const QString &id, const QString &text) {
	// Called from inside the engine: destroy it on the next turn.
	crl::on_main(this, [=] {
		if (const auto plugin = find(id)) {
			stop(id);
			plugin->enabled = false;
			plugin->lastError = TruncateText(text.toStdString());
			append(id, Js::LogLevel::Error, text);
			save();
			changes.fire({});
		}
	});
}

json Manager::Private::instanceSetting(
		const QString &id,
		const QString &key) {
	const auto parsed = manifest(id);
	if (!parsed) {
		return nullptr;
	}
	const auto name = key.toStdString();
	for (const auto &setting : parsed->settings) {
		if (setting.key == name) {
			const auto &values = settingsOf(id);
			const auto i = values.find(name);
			return (i != values.end() && SettingAccepts(setting, *i))
				? *i
				: SettingDefault(setting);
		}
	}
	return nullptr;
}

Manager::Manager() : _private(std::make_unique<Private>()) {
	Expects(!Current);

	Current = this;
	_private->load();
}

void Manager::startAll() {
	if (cNoPlugins()) {
		LOG(("Luxury Plugins: not started, -noplugins is set."));
		return;
	} else if (QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier) {
		// Held Shift is -noplugins for this launch: a plugin that takes the
		// client down can be disabled without the command line.
		LOG(("Luxury Plugins: not started, Shift was held at launch."));
		cSetNoPlugins(true);
		return;
	}
	auto queue = std::deque<QString>();
	for (const auto &plugin : _private->registry.plugins) {
		queue.push_back(Text(plugin.id));
	}
	_private->startNext(std::move(queue));
}

Manager::~Manager() {
	while (!_private->running.empty()) {
		_private->stop(_private->running.begin()->first);
	}
	Current = nullptr;
}

Manager &Manager::Instance() {
	Expects(Current != nullptr);

	return *Current;
}

Manager *Manager::Maybe() {
	return Current;
}

rpl::producer<> Manager::changes() const {
	return _private->changes.events();
}

std::vector<PluginView> Manager::plugins() const {
	auto result = std::vector<PluginView>();
	for (const auto &plugin : _private->registry.plugins) {
		if (auto view = this->plugin(Text(plugin.id))) {
			result.push_back(std::move(*view));
		}
	}
	return result;
}

std::optional<PluginView> Manager::plugin(const QString &id) const {
	const auto plugin = _private->find(id);
	if (!plugin) {
		return std::nullopt;
	}
	auto result = PluginView{
		.id = id,
		.name = Text(plugin->name),
		.version = Text(plugin->version),
		.description = Text(plugin->description),
		.author = Text(plugin->author),
		.homepage = Text(plugin->homepage),
		.sourceLabel = Text(SourceLabel(
			plugin->source.owner,
			plugin->source.repo,
			plugin->source.channel,
			plugin->source.ref,
			plugin->source.label)),
		.requested = plugin->requested,
		.granted = plugin->granted,
		.enabled = plugin->enabled,
		.running = _private->running.contains(id),
		.lastError = (plugin->lastError == kQuarantinedError)
			? tr::lng_luxury_plugins_quarantined(tr::now)
			: Text(plugin->lastError),
		.pinned = plugin->pinned,
		.previousVersion = Text(plugin->previousVersion),
	};
	for (const auto &host : plugin->network) {
		result.network.push_back(Text(host));
	}
	if (const auto parsed = _private->manifest(id)) {
		result.settings = parsed->settings;
	}
	if (const auto i = _private->updates.find(id)
		; i != _private->updates.end()) {
		result.updateAvailable = true;
		result.availableLabel = i->second;
	}
	return result;
}

std::vector<LogLine> Manager::logs(const QString &id) const {
	const auto i = _private->logs.find(id);
	return (i != _private->logs.end())
		? std::vector<LogLine>(i->second.begin(), i->second.end())
		: std::vector<LogLine>();
}

void Manager::clearLogs(const QString &id) {
	_private->logs.erase(id);
}

void Manager::setEnabled(const QString &id, bool enabled) {
	const auto plugin = _private->find(id);
	if (!plugin || plugin->enabled == enabled) {
		return;
	}
	if (enabled) {
		plugin->enabled = true;
		plugin->lastError.clear();
		_private->save();
		_private->start(id, "enable");
	} else {
		_private->stop(id);
		plugin->enabled = false;
		_private->save();
	}
	_private->changes.fire({});
}

void Manager::remove(const QString &id) {
	if (!_private->find(id) || _private->committing.contains(id)) {
		return;
	}
	_private->stop(id);
	auto &list = _private->registry.plugins;
	const auto key = id.toStdString();
	list.erase(
		std::remove_if(list.begin(), list.end(), [&](const auto &plugin) {
			return plugin.id == key;
		}),
		list.end());
	_private->save();
	QDir(PluginRoot(id)).removeRecursively();
	_private->manifests.erase(id);
	_private->logs.erase(id);
	_private->settings.erase(id);
	_private->updates.erase(id);
	_private->changes.fire({});
}

void Manager::reload(const QString &id) {
	const auto plugin = _private->find(id);
	if (!plugin || !plugin->enabled) {
		return;
	}
	_private->stop(id);
	plugin->lastError.clear();
	_private->manifests.erase(id);
	_private->save();
	_private->start(id, "reload");
	_private->changes.fire({});
}

json Manager::settingValue(const QString &id, const QString &key) const {
	return _private->instanceSetting(id, key);
}

void Manager::setSettingValue(
		const QString &id,
		const QString &key,
		json value) {
	const auto parsed = _private->manifest(id);
	if (!parsed) {
		return;
	}
	const auto name = key.toStdString();
	const auto i = std::find_if(
		parsed->settings.begin(),
		parsed->settings.end(),
		[&](const SettingDescriptor &setting) { return setting.key == name; });
	if (i == parsed->settings.end() || !SettingAccepts(*i, value)) {
		return;
	}
	auto &values = _private->settingsOf(id);
	values[name] = value;
	if (!WriteFile(
			PluginRoot(id) + u"/data/settings.json"_q,
			QByteArray::fromStdString(values.dump()))) {
		_private->append(
			id,
			Js::LogLevel::Error,
			u"Could not write data/settings.json."_q);
	}
	if (const auto j = _private->running.find(id)
		; j != _private->running.end()) {
		j->second->settingChanged(key, value);
	}
}

void Manager::Private::buildPlan(
		GithubInput input,
		Resolution resolution,
		QString updating,
		bool fallback,
		Fn<void(InstallPlan, QString)> done) {
	if (!resolution.error.isEmpty()) {
		done(Failed(resolution.code), resolution.error);
		return;
	}
	const auto source = PluginSource{
		.owner = input.owner,
		.repo = input.repo,
		.subdir = input.subdir,
		.channel = resolution.ref.channel,
		.ref = resolution.ref.ref,
		.label = resolution.ref.label,
	};
	const auto weak = base::make_weak(this);
	installer.manifest(source, [=](
			Manifest fetched,
			QByteArray raw,
			QString error,
			ErrorCode code) {
		const auto d = weak.get();
		if (!d) {
			return;
		} else if (fallback
			&& code == ErrorCode::NotFound
			&& source.channel == Channel::Release) {
			// The latest release has no plugin.json: offer the default
			// branch instead and say so.
			const auto tag = Text(source.ref);
			d->installer.defaultBranch(input.owner, input.repo, [=](
					Resolution branch) {
				const auto d = weak.get();
				if (!d) {
					return;
				} else if (branch.error.isEmpty()) {
					branch.notice = tr::lng_luxury_plugins_release_fallback(
						tr::now,
						lt_tag,
						tag,
						lt_name,
						Text(branch.ref.label));
				}
				d->buildPlan(input, std::move(branch), updating, false, done);
			});
			return;
		} else if (!error.isEmpty()) {
			done(Failed(code), error);
			return;
		} else if (const auto text = CompatibilityError(
				fetched,
				LuxuryVersionStr); !text.empty()) {
			done(Failed(ErrorCode::Incompatible), Text(text));
			return;
		}
		const auto id = Text(fetched.id);
		const auto installed = d->find(updating.isEmpty() ? id : updating);
		const auto other = d->installedFrom(
			input.owner,
			input.repo,
			input.subdir);
		if (!updating.isEmpty() && !installed) {
			done(Failed(ErrorCode::NotFound), u"The plugin was removed."_q);
			return;
		} else if (!updating.isEmpty() && id != updating) {
			done(
				Failed(ErrorCode::Conflict),
				u"The new version has the id \"%1\", not \"%2\"."_q
					.arg(id, updating));
			return;
		} else if (installed
			&& !SameRepo(
				installed->source.owner,
				installed->source.repo,
				input.owner,
				input.repo)) {
			// Another repository must not take over the id, its grants
			// and its data.
			done(Failed(ErrorCode::Conflict), u"A plugin with the id \"%1\" "
				"is already installed from %2/%3."_q.arg(
					id,
					Text(installed->source.owner),
					Text(installed->source.repo)));
			return;
		} else if (other && other != installed) {
			done(
				Failed(ErrorCode::Conflict),
				tr::lng_luxury_plugins_repo_installed(
					tr::now,
					lt_name,
					Text(other->name)));
			return;
		}
		auto plan = InstallPlan{
			.input = input,
			.resolved = resolution.ref,
			.manifest = std::move(fetched),
			.update = (installed != nullptr),
			.manifestBytes = raw.toStdString(),
		};
		auto storedId = std::int64_t();
		auto decision = UpdateDecision::Available;
		if (installed) {
			plan.previouslyGranted = installed->granted;
			decision = DecideUpdate(
				StoredRef(installed->source),
				plan.resolved);
			plan.downgrade = (decision == UpdateDecision::Downgrade);
			storedId = installed->source.repoId;
		}
		plan.newPermissions = MissingPermissions(
			plan.manifest.permissions,
			plan.previouslyGranted);
		plan.notice = resolution.notice;
		if (plan.downgrade && !updating.isEmpty()) {
			// An update never goes back; an install link may, with the
			// allowDowngrade flag.
			done(
				Failed(ErrorCode::Downgrade),
				tr::lng_luxury_plugins_downgrade(tr::now));
			return;
		} else if (decision == UpdateDecision::UpToDate
			&& !updating.isEmpty()) {
			done(
				Failed(ErrorCode::UpToDate),
				tr::lng_luxury_plugins_up_to_date(tr::now));
			return;
		}
		d->installer.identify(input.owner, input.repo, plan.resolved, [=](
				Identity identity) {
			if (RepoReplaced(storedId, identity.repoId)) {
				done(
					Failed(ErrorCode::Conflict),
					tr::lng_luxury_plugins_repo_replaced(tr::now));
				return;
			}
			auto copy = plan;
			copy.commit = identity.commit;
			copy.repoId = identity.repoId;
			done(std::move(copy), QString());
		});
	});
}

void Manager::prepareInstall(
		const QString &link,
		Fn<void(InstallPlan plan, QString error)> done) {
	const auto input = ParseGithubInput(link.trimmed().toStdString());
	if (!input) {
		done(Failed(ErrorCode::Link), Text(input.error()));
		return;
	}
	const auto weak = base::make_weak(_private.get());
	const auto parsed = input.value();
	_private->installer.resolve(parsed, [=](Resolution resolution) {
		const auto d = weak.get();
		if (!d) {
			return;
		}
		// A tree/ link may have moved segments into the branch name.
		auto chosen = parsed;
		chosen.subdir = resolution.subdir;
		d->buildPlan(
			std::move(chosen),
			std::move(resolution),
			QString(),
			(parsed.kind == GithubKind::Auto),
			done);
	});
}

void Manager::prepareUpdate(
		const QString &id,
		Fn<void(InstallPlan plan, QString error)> done) {
	const auto plugin = _private->find(id);
	if (!plugin) {
		done(Failed(ErrorCode::NotFound), u"The plugin is not installed."_q);
		return;
	} else if (plugin->pinned) {
		done(
			Failed(ErrorCode::Pinned),
			tr::lng_luxury_plugins_pinned(tr::now));
		return;
	}
	const auto weak = base::make_weak(_private.get());
	const auto stored = plugin->source;
	const auto input = GithubInput{
		.owner = stored.owner,
		.repo = stored.repo,
		.subdir = stored.subdir,
	};
	_private->installer.latest(stored, [=](Resolution resolution) {
		if (const auto d = weak.get()) {
			d->buildPlan(input, std::move(resolution), id, false, done);
		}
	});
}

void Manager::commitInstall(
		InstallPlan plan,
		Fn<void(QString error, ErrorCode code)> done) {
	const auto id = Text(plan.manifest.id);
	const auto installed = _private->find(id);
	const auto other = _private->installedFrom(
		plan.input.owner,
		plan.input.repo,
		plan.input.subdir);
	if (!IsValidPluginId(plan.manifest.id)) {
		done(u"The plugin id is not valid."_q, ErrorCode::Manifest);
		return;
	} else if (plan.update != (installed != nullptr)) {
		done(u"The plugin list changed, try again."_q, ErrorCode::Conflict);
		return;
	} else if (installed
		&& !SameRepo(
			installed->source.owner,
			installed->source.repo,
			plan.input.owner,
			plan.input.repo)) {
		done(
			u"The plugin is installed from another repository."_q,
			ErrorCode::Conflict);
		return;
	} else if (other && other != installed) {
		done(tr::lng_luxury_plugins_repo_installed(
			tr::now,
			lt_name,
			Text(other->name)), ErrorCode::Conflict);
		return;
	} else if (installed
		&& RepoReplaced(installed->source.repoId, plan.repoId)) {
		done(
			tr::lng_luxury_plugins_repo_replaced(tr::now),
			ErrorCode::Conflict);
		return;
	} else if (installed
		&& !AcceptUpdate(
			DecideUpdate(StoredRef(installed->source), plan.resolved),
			plan.allowDowngrade)) {
		// Recomputed from the registry, plan.downgrade is a hint only.
		done(
			tr::lng_luxury_plugins_downgrade(tr::now),
			ErrorCode::Downgrade);
		return;
	} else if (_private->committing.contains(id)) {
		done(u"The plugin is being installed already."_q, ErrorCode::Busy);
		return;
	} else if (const auto text = CompatibilityError(
			plan.manifest,
			LuxuryVersionStr); !text.empty()) {
		done(Text(text), ErrorCode::Incompatible);
		return;
	}
	// Recomputed from the registry, never trusted from the plan.
	const auto previously = installed ? installed->granted : PermissionSet();
	const auto newPermissions = MissingPermissions(
		plan.manifest.permissions,
		previously);
	const auto widened = installed && NetworkWidened(
		installed->granted.has(Permission::Network),
		installed->network,
		plan.manifest.permissions,
		plan.manifest.network);
	if (NeedsConsent(plan.update, newPermissions, widened, plan.consented)) {
		done(
			u"The plugin asks for new access, review it first."_q,
			ErrorCode::Consent);
		return;
	}
	const auto source = PluginSource{
		.owner = plan.input.owner,
		.repo = plan.input.repo,
		.subdir = plan.input.subdir,
		.channel = plan.resolved.channel,
		.ref = plan.resolved.ref,
		.label = plan.resolved.label,
		.commit = plan.commit,
		.repoId = (plan.repoId > 0 || !installed)
			? plan.repoId
			: installed->source.repoId,
	};
	_private->committing.emplace(id);
	const auto weak = base::make_weak(_private.get());
	const auto staging = Root()
		+ u".staging/"_q
		+ QUuid::createUuid().toString(QUuid::Id128);
	const auto finish = [=](QString error, ErrorCode code) {
		QDir(staging).removeRecursively();
		if (const auto d = weak.get()) {
			d->committing.erase(id);
		}
		done(error, code);
	};
	const auto reviewed = plan.manifestBytes;
	const auto shown = plan.consented
		? plan.shownPermissions
		: PermissionSet();
	const auto shownHosts = plan.shownHosts;
	const auto notice = plan.notice;
	_private->installer.manifest(source, [=](
			Manifest manifest,
			QByteArray raw,
			QString error,
			ErrorCode code) {
		const auto d = weak.get();
		if (!d) {
			return;
		} else if (!error.isEmpty()) {
			finish(error, code);
			return;
		} else if (raw.toStdString() != reviewed) {
			finish(u"plugin.json changed since it was reviewed, "
				"try again."_q, ErrorCode::Conflict);
			return;
		}
		d->installer.download(source, manifest.files, staging + u"/code"_q, [=](
				QString error) {
			const auto d = weak.get();
			if (!d) {
				return;
			} else if (!error.isEmpty()) {
				finish(error, ErrorCode::Network);
				return;
			}
			// files may not list plugin.json: keep the reviewed bytes.
			if (!WriteFile(staging + u"/code/plugin.json"_q, raw)) {
				finish(u"Could not write plugin.json."_q, ErrorCode::Files);
				return;
			}
			const auto existing = d->find(id);
			if (plan.update != (existing != nullptr)) {
				finish(
					u"The plugin list changed, try again."_q,
					ErrorCode::Conflict);
				return;
			}
			// Against the registry as it is now, not as it was at prepare:
			// anything the user did not see in the consent box fails.
			const auto previously = existing
				? existing->granted
				: PermissionSet();
			const auto grant = CommitGrant(
				manifest.permissions,
				previously,
				shown,
				existing && NetworkWidened(
					previously.has(Permission::Network),
					existing->network,
					manifest.permissions,
					manifest.network),
				manifest.network,
				shownHosts);
			if (!grant) {
				finish(
					u"The plugin changed, review it again."_q,
					ErrorCode::Consent);
				return;
			}
			d->stop(id);
			const auto target = PluginRoot(id) + u"/code"_q;
			// pending.json lets the next launch finish or undo the swap if
			// the client dies before the registry names the new files.
			const auto swapped = [&]() -> std::string {
				try {
					return SwapWithJournal(
						FsPath(staging + u"/code"_q),
						FsPath(target),
						source.ref,
						!existing,
						false);
				} catch (const std::exception &e) {
					return e.what(); // Path conversion or allocation.
				}
			}();
			if (!swapped.empty()) {
				d->start(id, "reload"); // The old version keeps running.
				finish(u"Could not replace the plugin files: "_q
					+ QString::fromLocal8Bit(swapped.c_str()), ErrorCode::Files);
				return;
			}
			auto entry = existing ? *existing : InstalledPlugin();
			if (existing) {
				entry.previousVersion = existing->version;
				entry.previousSource = existing->source;
			}
			entry.id = manifest.id;
			entry.name = manifest.name;
			entry.version = manifest.version;
			entry.description = manifest.description;
			entry.author = manifest.author;
			entry.homepage = manifest.homepage;
			entry.requested = manifest.permissions;
			entry.network = manifest.network;
			entry.source = source;
			entry.granted = *grant;
			entry.enabled = existing ? existing->enabled : true;
			entry.installedAt = QDateTime::currentSecsSinceEpoch();
			entry.lastError.clear();
			if (existing) {
				*existing = std::move(entry);
			} else {
				d->registry.plugins.push_back(std::move(entry));
			}
			d->manifests.erase(id);
			d->updates.erase(id);
			if (d->save()) {
				FinishSwap(FsPath(target));
			}
			if (!notice.isEmpty()) {
				d->append(id, Js::LogLevel::Info, notice);
			}
			d->start(id, plan.update ? "update" : "install");
			d->changes.fire({});
			finish(QString(), ErrorCode::None);
		});
	});
}

void Manager::commitInstall(
		InstallPlan plan,
		Fn<void(QString error)> done) {
	commitInstall(std::move(plan), [=](QString error, ErrorCode) {
		done(error);
	});
}

void Manager::rollback(
		const QString &id,
		Fn<void(QString error, ErrorCode code)> done) {
	const auto plugin = _private->find(id);
	const auto code = PluginRoot(id) + u"/code"_q;
	if (!plugin) {
		done(u"The plugin is not installed."_q, ErrorCode::NotFound);
		return;
	} else if (_private->committing.contains(id)) {
		done(u"The plugin is being installed already."_q, ErrorCode::Busy);
		return;
	} else if (plugin->previousVersion.empty()
		|| !QFileInfo::exists(code + u".prev/plugin.json"_q)) {
		done(
			tr::lng_luxury_plugins_no_previous(tr::now),
			ErrorCode::NotFound);
		return;
	}
	_private->stop(id);
	const auto swapped = SwapWithJournal(
		FsPath(code + u".prev"_q),
		FsPath(code),
		plugin->previousSource.ref,
		false,
		true);
	if (!swapped.empty()) {
		_private->start(id, "reload");
		done(u"Could not replace the plugin files: "_q
			+ QString::fromLocal8Bit(swapped.c_str()), ErrorCode::Files);
		return;
	}
	// The files rolled back from become code.prev in FinishSwap.
	std::swap(plugin->version, plugin->previousVersion);
	std::swap(plugin->source, plugin->previousSource);
	_private->manifests.erase(id);
	if (const auto parsed = _private->manifest(id)) {
		plugin->name = parsed->name;
		plugin->version = parsed->version;
		plugin->description = parsed->description;
		plugin->author = parsed->author;
		plugin->homepage = parsed->homepage;
		plugin->requested = parsed->permissions;
		// Never more than the user granted to the version rolled back
		// from: the old manifest is not reviewed again.
		plugin->granted = PermissionSet(
			plugin->granted.bits() & parsed->permissions.bits());
		auto hosts = std::vector<std::string>();
		for (const auto &host : parsed->network) {
			if (std::find(
					plugin->network.begin(),
					plugin->network.end(),
					host) != plugin->network.end()) {
				hosts.push_back(host);
			}
		}
		plugin->network = std::move(hosts);
	}
	plugin->lastError.clear();
	_private->updates.erase(id);
	if (_private->save()) {
		FinishSwap(FsPath(code));
	}
	_private->start(id, "rollback");
	_private->changes.fire({});
	done(QString(), ErrorCode::None);
}

void Manager::setPinned(const QString &id, bool pinned) {
	const auto plugin = _private->find(id);
	if (!plugin || plugin->pinned == pinned) {
		return;
	}
	plugin->pinned = pinned;
	if (pinned) {
		_private->updates.erase(id);
	}
	_private->save();
	_private->changes.fire({});
}

void Manager::checkUpdatesDetailed(Fn<void(UpdateCheck result)> done) {
	auto &waiters = _private->checkWaiters;
	waiters.push_back(std::move(done));
	if (waiters.size() > 1) {
		return; // A check is running, its result goes to every caller.
	}
	const auto weak = base::make_weak(_private.get());
	const auto finish = [=](const UpdateTally &tally) {
		const auto d = weak.get();
		if (!d) {
			return;
		}
		const auto result = UpdateCheck{
			.available = tally.available,
			.failed = tally.failed,
			.firstError = Text(tally.firstError),
			.firstCode = tally.firstCode,
		};
		const auto callbacks = std::exchange(
			d->checkWaiters,
			std::vector<Fn<void(UpdateCheck)>>());
		d->changes.fire({});
		for (const auto &callback : callbacks) {
			if (callback) {
				callback(result);
			}
		}
	};
	struct State {
		int left = 0;
		UpdateTally tally;
	};
	const auto state = std::make_shared<State>();
	auto checked = std::vector<InstalledPlugin>();
	for (const auto &plugin : _private->registry.plugins) {
		if (plugin.pinned) {
			_private->updates.erase(Text(plugin.id));
		} else {
			checked.push_back(plugin);
		}
	}
	if (checked.empty()) {
		finish(state->tally);
		return;
	}
	state->left = int(checked.size());
	for (const auto &plugin : checked) {
		const auto id = Text(plugin.id);
		const auto stored = StoredRef(plugin.source);
		_private->installer.latest(plugin.source, [=](Resolution latest) {
			const auto d = weak.get();
			if (!d) {
				return;
			}
			if (!latest.error.isEmpty()) {
				d->append(
					id,
					Js::LogLevel::Warn,
					u"Update check failed: "_q + latest.error);
				state->tally.add(
					latest.error.toStdString(),
					latest.code,
					false);
			} else {
				const auto found = (DecideUpdate(stored, latest.ref)
					== UpdateDecision::Available);
				if (found) {
					d->updates[id] = Text(AvailableLabel(latest.ref));
				} else {
					d->updates.erase(id);
				}
				state->tally.add({}, ErrorCode::None, found);
			}
			if (!--state->left) {
				finish(state->tally);
			}
		});
	}
}

void Manager::checkUpdates(Fn<void(int available)> done) {
	checkUpdatesDetailed([=](UpdateCheck result) {
		if (done) {
			done(result.available);
		}
	});
}

bool Manager::active() const {
	return !_private->running.empty();
}

std::vector<Manager::MenuItem> Manager::messageMenuItems() const {
	auto result = std::vector<MenuItem>();
	for (const auto &[id, instance] : _private->running) {
		if (!instance->plugin().granted.has(Permission::Ui)) {
			continue;
		}
		for (const auto &item : instance->menuItems()) {
			result.push_back({ id, item.title, item.fn });
		}
	}
	return result;
}

std::vector<Manager::Command> Manager::commands() const {
	auto result = std::vector<Command>();
	for (const auto &[id, instance] : _private->running) {
		for (const auto &command : instance->commands()) {
			result.push_back({ id, command.title, command.description });
		}
	}
	return result;
}

std::vector<not_null<Plugins::Instance*>> Manager::instances() const {
	auto result = std::vector<not_null<Plugins::Instance*>>();
	result.reserve(_private->running.size());
	for (const auto &[id, instance] : _private->running) {
		result.push_back(instance.get());
	}
	return result;
}

} // namespace Luxury::Plugins
