// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/plugin_manager.h"

#include "luxury/plugins/core/plugin_registry.h"
#include "luxury/plugins/plugin_host_api.h"
#include "luxury/plugins/plugin_host_logic.h"
#include "luxury/plugins/plugin_installer.h"
#include "base/call_delayed.h"
#include "core/version.h"
#include "logs.h"
#include "settings.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QUuid>

#include <deque>
#include <map>
#include <set>

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
	Installer installer;
	rpl::event_stream<> changes;
	bool changesQueued = false;

	[[nodiscard]] InstalledPlugin *find(const QString &id);
	[[nodiscard]] const Manifest *manifest(const QString &id);
	void load();
	void save();
	void append(const QString &id, Js::LogLevel level, const QString &text);
	void setError(const QString &id, const QString &text);
	void fireChangesLater();
	void start(const QString &id);
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
	auto file = QFile(Root() + u"registry.json"_q);
	if (!file.exists()) {
		return;
	} else if (!file.open(QIODevice::ReadOnly)) {
		LOG(("Luxury Plugins: could not read registry.json."));
		return;
	}
	auto parsed = ParseRegistry(file.readAll().toStdString());
	file.close();
	if (!parsed) {
		// Keep the broken file for inspection; the next save starts over.
		LOG(("Luxury Plugins: registry.json is broken: %1"
			).arg(Text(parsed.error())));
		QFile::remove(Root() + u"registry.json.bad"_q);
		QFile::rename(
			Root() + u"registry.json"_q,
			Root() + u"registry.json.bad"_q);
		return;
	}
	registry = std::move(parsed.value());
	for (const auto &skipped : registry.skipped) {
		LOG(("Luxury Plugins: registry entry skipped, %1"
			).arg(Text(skipped)));
	}
}

void Manager::Private::save() {
	auto copy = registry;
	copy.skipped.clear();
	if (!WriteFile(
			Root() + u"registry.json"_q,
			QByteArray::fromStdString(SerializeRegistry(copy)))) {
		LOG(("Luxury Plugins: could not write registry.json."));
	}
}

void Manager::Private::append(
		const QString &id,
		Js::LogLevel level,
		const QString &text) {
	auto &lines = logs[id];
	lines.push_back({
		.time = QDateTime::currentMSecsSinceEpoch(),
		.level = int(level),
		.text = Text(TruncateText(text.toStdString())),
	});
	while (lines.size() > std::size_t(kLogLimit)) {
		lines.pop_front();
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

void Manager::Private::start(const QString &id) {
	const auto plugin = find(id);
	if (!plugin || !plugin->enabled || running.contains(id)) {
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
	auto instance = std::make_unique<Plugins::Instance>(
		*plugin,
		*parsed,
		PluginRoot(id),
		this);
	const auto raw = instance.get();
	running.emplace(id, std::move(instance));
	if (!raw->start()) {
		// The error is in the log and lastError already.
		running.erase(id);
	}
}

void Manager::Private::stop(const QString &id) {
	const auto i = running.find(id);
	if (i == running.end()) {
		return;
	}
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
	for (const auto &plugin : _private->registry.plugins) {
		_private->start(Text(plugin.id));
	}
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
		.lastError = Text(plugin->lastError),
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
		_private->start(id);
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
	_private->start(id);
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

void Manager::prepareInstall(
		const QString &link,
		Fn<void(InstallPlan plan, QString error)> done) {
	const auto input = ParseGithubInput(link.trimmed().toStdString());
	if (!input) {
		done({}, Text(input.error()));
		return;
	}
	const auto weak = base::make_weak(_private.get());
	const auto owner = input->owner;
	const auto repo = input->repo;
	const auto subdir = input->subdir;
	_private->installer.resolve(input.value(), [=](
			ResolvedRef resolved,
			QString error) {
		const auto d = weak.get();
		if (!d) {
			return;
		} else if (!error.isEmpty()) {
			done({}, error);
			return;
		}
		const auto source = PluginSource{
			.owner = owner,
			.repo = repo,
			.subdir = subdir,
			.channel = resolved.channel,
			.ref = resolved.ref,
			.label = resolved.label,
		};
		d->installer.manifest(source, [=](
				Manifest manifest,
				QByteArray raw,
				QString error) {
			const auto d = weak.get();
			if (!d) {
				return;
			} else if (!error.isEmpty()) {
				done({}, error);
				return;
			} else if (const auto text = CompatibilityError(
					manifest,
					LuxuryVersionStr); !text.empty()) {
				done({}, Text(text));
				return;
			}
			const auto installed = d->find(Text(manifest.id));
			if (installed
				&& (installed->source.owner != owner
					|| installed->source.repo != repo)) {
				// Another repository must not take over the id, its
				// grants and its data.
				done({}, u"A plugin with the id \"%1\" is already "
					"installed from %2/%3."_q.arg(
						Text(manifest.id),
						Text(installed->source.owner),
						Text(installed->source.repo)));
				return;
			}
			auto plan = InstallPlan{
				.input = input.value(),
				.resolved = resolved,
				.manifest = std::move(manifest),
				.update = (installed != nullptr),
				.manifestBytes = raw.toStdString(),
			};
			if (installed) {
				plan.previouslyGranted = installed->granted;
			}
			plan.newPermissions = MissingPermissions(
				plan.manifest.permissions,
				plan.previouslyGranted);
			done(std::move(plan), QString());
		});
	});
}

void Manager::prepareUpdate(
		const QString &id,
		Fn<void(InstallPlan plan, QString error)> done) {
	const auto plugin = _private->find(id);
	if (!plugin) {
		done({}, u"The plugin is not installed."_q);
		return;
	}
	const auto weak = base::make_weak(_private.get());
	const auto stored = plugin->source;
	_private->installer.latest(stored, [=](
			ResolvedRef resolved,
			QString error) {
		const auto d = weak.get();
		if (!d) {
			return;
		} else if (!error.isEmpty()) {
			done({}, error);
			return;
		}
		auto source = stored;
		source.channel = resolved.channel;
		source.ref = resolved.ref;
		source.label = resolved.label;
		d->installer.manifest(source, [=](
				Manifest manifest,
				QByteArray raw,
				QString error) {
			const auto d = weak.get();
			if (!d) {
				return;
			}
			const auto installed = d->find(id);
			if (!error.isEmpty()) {
				done({}, error);
				return;
			} else if (!installed) {
				done({}, u"The plugin was removed."_q);
				return;
			} else if (Text(manifest.id) != id) {
				done({}, u"The new version has the id \"%1\", not \"%2\"."_q
					.arg(Text(manifest.id), id));
				return;
			} else if (const auto text = CompatibilityError(
					manifest,
					LuxuryVersionStr); !text.empty()) {
				done({}, Text(text));
				return;
			}
			auto plan = InstallPlan{
				.input = {
					.owner = stored.owner,
					.repo = stored.repo,
					.subdir = stored.subdir,
				},
				.resolved = resolved,
				.manifest = std::move(manifest),
				.update = true,
				.previouslyGranted = installed->granted,
				.manifestBytes = raw.toStdString(),
			};
			plan.newPermissions = MissingPermissions(
				plan.manifest.permissions,
				plan.previouslyGranted);
			done(std::move(plan), QString());
		});
	});
}

void Manager::commitInstall(
		InstallPlan plan,
		Fn<void(QString error)> done) {
	const auto id = Text(plan.manifest.id);
	const auto installed = _private->find(id);
	if (!IsValidPluginId(plan.manifest.id)) {
		done(u"The plugin id is not valid."_q);
		return;
	} else if (plan.update != (installed != nullptr)) {
		done(u"The plugin list changed, try again."_q);
		return;
	} else if (installed
		&& (installed->source.owner != plan.input.owner
			|| installed->source.repo != plan.input.repo)) {
		done(u"The plugin is installed from another repository."_q);
		return;
	} else if (_private->committing.contains(id)) {
		done(u"The plugin is being installed already."_q);
		return;
	} else if (const auto text = CompatibilityError(
			plan.manifest,
			LuxuryVersionStr); !text.empty()) {
		done(Text(text));
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
		done(u"The plugin asks for new access, review it first."_q);
		return;
	}
	const auto source = PluginSource{
		.owner = plan.input.owner,
		.repo = plan.input.repo,
		.subdir = plan.input.subdir,
		.channel = plan.resolved.channel,
		.ref = plan.resolved.ref,
		.label = plan.resolved.label,
	};
	_private->committing.emplace(id);
	const auto weak = base::make_weak(_private.get());
	const auto staging = Root()
		+ u".staging/"_q
		+ QUuid::createUuid().toString(QUuid::Id128);
	const auto finish = [=](QString error) {
		QDir(staging).removeRecursively();
		if (const auto d = weak.get()) {
			d->committing.erase(id);
		}
		done(error);
	};
	const auto reviewed = plan.manifestBytes;
	const auto shown = plan.consented
		? plan.shownPermissions
		: PermissionSet();
	const auto shownHosts = plan.shownHosts;
	_private->installer.manifest(source, [=](
			Manifest manifest,
			QByteArray raw,
			QString error) {
		const auto d = weak.get();
		if (!d) {
			return;
		} else if (!error.isEmpty()) {
			finish(error);
			return;
		} else if (raw.toStdString() != reviewed) {
			finish(u"plugin.json changed since it was reviewed, "
				"try again."_q);
			return;
		}
		d->installer.download(source, manifest.files, staging + u"/code"_q, [=](
				QString error) {
			const auto d = weak.get();
			if (!d) {
				return;
			} else if (!error.isEmpty()) {
				finish(error);
				return;
			}
			// files may not list plugin.json: keep the reviewed bytes.
			if (!WriteFile(staging + u"/code/plugin.json"_q, raw)) {
				finish(u"Could not write plugin.json."_q);
				return;
			}
			const auto existing = d->find(id);
			if (plan.update != (existing != nullptr)) {
				finish(u"The plugin list changed, try again."_q);
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
				finish(u"The plugin changed, review it again."_q);
				return;
			}
			d->stop(id);
			const auto swapped = [&]() -> std::string {
				try {
					return SwapDirectory(
						FsPath(staging + u"/code"_q),
						FsPath(PluginRoot(id) + u"/code"_q));
				} catch (const std::exception &e) {
					return e.what(); // Path conversion or allocation.
				}
			}();
			if (!swapped.empty()) {
				d->start(id); // The old version keeps running.
				finish(u"Could not replace the plugin files: "_q
					+ QString::fromLocal8Bit(swapped.c_str()));
				return;
			}
			auto entry = existing ? *existing : InstalledPlugin();
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
			d->save();
			d->start(id);
			d->changes.fire({});
			finish(QString());
		});
	});
}

void Manager::checkUpdates(Fn<void(int available)> done) {
	const auto &list = _private->registry.plugins;
	if (list.empty()) {
		done(0);
		return;
	}
	struct State {
		int left = 0;
		int available = 0;
	};
	const auto state = std::make_shared<State>(State{
		.left = int(list.size()),
	});
	const auto weak = base::make_weak(_private.get());
	for (const auto &plugin : list) {
		const auto id = Text(plugin.id);
		const auto stored = ResolvedRef{
			.channel = plugin.source.channel,
			.ref = plugin.source.ref,
			.label = plugin.source.label,
		};
		_private->installer.latest(plugin.source, [=](
				ResolvedRef latest,
				QString error) {
			const auto d = weak.get();
			if (!d) {
				return;
			}
			if (!error.isEmpty()) {
				d->append(
					id,
					Js::LogLevel::Warn,
					u"Update check failed: "_q + error);
			} else if (DecideUpdate(stored, latest)
					== UpdateDecision::Available) {
				d->updates[id] = Text(AvailableLabel(latest));
				++state->available;
			} else {
				d->updates.erase(id);
			}
			if (!--state->left) {
				d->changes.fire({});
				done(state->available);
			}
		});
	}
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
