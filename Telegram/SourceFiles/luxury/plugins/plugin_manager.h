// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include "luxury/plugins/core/plugin_manifest.h"
#include "luxury/plugins/core/plugin_permissions.h"
#include "luxury/plugins/core/plugin_source.h"
#include "luxury/plugins/plugin_install_logic.h"
#include "luxury/libs/json.hpp"

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace Luxury::Plugins {

class Instance;

// What the plugin manager UI shows for one installed plugin.
struct PluginView {
	QString id;
	QString name;
	QString version;
	QString description;
	QString author;
	QString homepage;
	QString sourceLabel; // "owner/repo@v1.2.0" or "owner/repo#main (abc1234)".
	PermissionSet requested;
	PermissionSet granted;
	std::vector<QString> network;
	std::vector<SettingDescriptor> settings;
	bool enabled = false;
	bool running = false; // Enabled and loaded without error.
	QString lastError;
	bool updateAvailable = false;
	QString availableLabel; // Tag or short SHA offered by checkUpdates.
	bool pinned = false; // checkUpdates skips it.
	QString previousVersion; // What rollback() restores, empty for none.
};

// A resolved install or update, shown to the user before anything is
// written. manifest is the downloaded plugin.json of the new version.
struct InstallPlan {
	GithubInput input;
	ResolvedRef resolved;
	Manifest manifest;
	bool update = false; // Replaces an installed plugin with this id.
	PermissionSet previouslyGranted; // Empty for a fresh install.
	// Requested permissions the user has not granted yet. A fresh install
	// lists all requested; an update with nothing new needs no consent.
	PermissionSet newPermissions;
	// The exact plugin.json bytes the plan was built from; commitInstall
	// installs only these bytes.
	std::string manifestBytes;
	// Set by the consent box when the user allowed this plan: what the box
	// listed. commitInstall refuses a fresh install, new permissions or
	// wider hosts without it, and grants only what was shown.
	bool consented = false;
	PermissionSet shownPermissions;
	std::vector<std::string> shownHosts;
	// resolved is an older release than the installed one: commitInstall
	// refuses it unless allowDowngrade is set.
	bool downgrade = false;
	bool allowDowngrade = false;
	// A fallback the user should know of, e.g. a release without
	// plugin.json installed from the default branch instead.
	QString notice;
	// Why prepare failed, set with the error text.
	ErrorCode errorCode = ErrorCode::None;
	// The commit and repository id resolved points at, best effort.
	std::string commit;
	std::int64_t repoId = 0;
};

struct LogLine {
	qint64 time = 0; // Unix milliseconds.
	int level = 0; // Js::LogLevel as int.
	QString text;
};

// Owns every plugin: registry, engines, installs and the client hooks.
// Lives in Core::Application, main thread only. Every callback runs on the
// main thread; a callback is not called after the manager is destroyed.
class Manager final {
public:
	Manager();
	~Manager();

	[[nodiscard]] static Manager &Instance();
	// Null before Core::App creates it and after shutdown.
	[[nodiscard]] static Manager *Maybe();

	// Fired after any change of the list, state, errors or update flags.
	[[nodiscard]] rpl::producer<> changes() const;

	[[nodiscard]] std::vector<PluginView> plugins() const;
	[[nodiscard]] std::optional<PluginView> plugin(const QString &id) const;
	[[nodiscard]] std::vector<LogLine> logs(const QString &id) const;
	void clearLogs(const QString &id);
	// Fires the plugin id after each new log line.
	[[nodiscard]] rpl::producer<QString> logAdded() const;

	void setEnabled(const QString &id, bool enabled);
	void remove(const QString &id);
	// Restarts an enabled plugin, e.g. after its last error.
	void reload(const QString &id);

	// Manifest settings, JSON values of the declared type.
	[[nodiscard]] nlohmann::json settingValue(
		const QString &id,
		const QString &key) const;
	void setSettingValue(
		const QString &id,
		const QString &key,
		nlohmann::json value);

	// Parses the link, resolves the ref and downloads plugin.json only.
	// done gets the plan or a human readable error.
	void prepareInstall(
		const QString &link,
		Fn<void(InstallPlan plan, QString error)> done);
	// Same for the installed plugin's stored source; UpToDate when the
	// latest ref is the installed one.
	void prepareUpdate(
		const QString &id,
		Fn<void(InstallPlan plan, QString error)> done);
	// Downloads every file into staging, swaps it in, grants the previous
	// grant plus what the consent box showed and starts the plugin when it
	// was enabled (a fresh install is enabled). Anything the user was not
	// shown fails with "review it again" (Consent); done gets an empty
	// error and ErrorCode::None on success, Busy while another commit of the
	// plugin runs and Files when the files could not be written or swapped.
	void commitInstall(
		InstallPlan plan,
		Fn<void(QString error, ErrorCode code)> done);
	// The same, for a caller that shows the text only.
	void commitInstall(InstallPlan plan, Fn<void(QString error)> done);
	// Marks a running commitInstall of the plugin: when its current fetch
	// ends, before the files are written, done gets Busy. Requests in flight
	// are not aborted; nothing happens once the swap started.
	void cancelInstall(const QString &id);
	// Puts back the version code.prev holds, see PluginView::previousVersion.
	// Busy while an install of it runs, Files when the swap failed.
	void rollback(
		const QString &id,
		Fn<void(QString error, ErrorCode code)> done);
	// A pinned plugin is skipped by checkUpdates and refuses prepareUpdate.
	void setPinned(const QString &id, bool pinned);

	struct UpdateCheck {
		int available = 0;
		int failed = 0;
		QString firstError;
		ErrorCode firstCode = ErrorCode::None;
	};
	// Refreshes updateAvailable of every plugin that is not pinned. A call
	// while a check runs gets that check's result.
	void checkUpdatesDetailed(Fn<void(UpdateCheck result)> done);
	// Same, done gets how many have an update.
	void checkUpdates(Fn<void(int available)> done);

	// Starts every enabled plugin, once the first window is shown, one per
	// event loop turn. Nothing starts with -noplugins or Shift held.
	void startAll();

	// --- Client hooks, see docs/plugins/ARCHITECTURE.md "Client hooks".

	// Single bool check for the hook call sites.
	[[nodiscard]] bool active() const;

	struct MenuItem {
		QString pluginId;
		QString title;
		std::int64_t handler = 0;
	};
	struct Command {
		QString pluginId;
		QString name;
		QString description;
	};
	[[nodiscard]] std::vector<MenuItem> messageMenuItems() const;
	[[nodiscard]] std::vector<Command> commands() const;
	// Running plugins for plugin_hooks.cpp, valid until the next change.
	[[nodiscard]] std::vector<not_null<Plugins::Instance*>> instances() const;

private:
	struct Private;
	std::unique_ptr<Private> _private;

};

[[nodiscard]] QString PermissionTitle(Permission permission);
[[nodiscard]] QString PermissionDescription(Permission permission);

} // namespace Luxury::Plugins
