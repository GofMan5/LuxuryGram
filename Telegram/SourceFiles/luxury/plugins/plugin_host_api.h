// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include "luxury/plugins/core/plugin_manifest.h"
#include "luxury/plugins/core/plugin_registry.h"
#include "luxury/plugins/js/js_engine.h"
#include "luxury/plugins/plugin_host_logic.h"
#include "base/weak_ptr.h"

#include <QString>

#include <map>
#include <memory>
#include <set>
#include <vector>

class QNetworkAccessManager;
class QTimer;

namespace Main {
class Session;
} // namespace Main

namespace Luxury::Plugins {
namespace Tl {
class Schema;
} // namespace Tl

// gzip_packed data of api.call results and updates, capped at 32 MiB.
[[nodiscard]] std::optional<std::string> Gunzip(std::string_view packed);
// The embedded api.tl, parsed once; null with error when it is broken.
[[nodiscard]] const Tl::Schema *ApiSchema(std::string &error);

// What a running plugin reports back to the manager.
class InstanceDelegate {
public:
	virtual ~InstanceDelegate() = default;

	virtual void instanceLog(
		const QString &id,
		Js::LogLevel level,
		const QString &text) = 0;
	// An entry failed: the error is logged and becomes lastError.
	virtual void instanceError(const QString &id, const QString &text) = 0;
	// The plugin went over its CpuBudget; the manager disables it later.
	virtual void instanceStop(const QString &id, const QString &text) = 0;
	[[nodiscard]] virtual nlohmann::json instanceSetting(
		const QString &id,
		const QString &key) = 0;

};

// One enabled plugin: its engine and everything the lux.* API holds.
// Every engine entry made from a host callback runs on a later event loop
// turn, so the engine is never entered from inside itself.
class Instance final : public Js::Host, public base::has_weak_ptr {
public:
	struct Handler {
		QString title; // Command name, menu title or key sequence.
		QString description;
		std::int64_t fn = 0;
		int keys = 0; // Hotkeys: Qt key | modifiers.
	};

	Instance(
		InstalledPlugin plugin,
		Manifest manifest,
		QString root,
		not_null<InstanceDelegate*> delegate);
	~Instance();

	// Loads main; false when it failed (the error is reported already).
	[[nodiscard]] bool start();
	// Fires 'unload' synchronously; the instance is destroyed next.
	void unload();

	[[nodiscard]] const InstalledPlugin &plugin() const {
		return _plugin;
	}
	[[nodiscard]] bool wants(const std::string &event) const;
	[[nodiscard]] const std::vector<Handler> &commands() const {
		return _commands;
	}
	[[nodiscard]] const std::vector<Handler> &menuItems() const {
		return _menuItems;
	}
	[[nodiscard]] const std::vector<Handler> &hotkeys() const {
		return _hotkeys;
	}

	// On the next event loop turn.
	void emitLater(std::string event, nlohmann::json payload);
	void callLater(std::int64_t fn, nlohmann::json args);
	void settingChanged(const QString &key, const nlohmann::json &value);

	// Synchronous, from ApiWrap::sendMessage: false, a string or null.
	[[nodiscard]] nlohmann::json beforeSend(const nlohmann::json &payload);

	Js::HostResult call(
		std::string_view function,
		nlohmann::json args,
		const Js::CallContext &context) override;
	void log(Js::LogLevel level, std::string_view text) noexcept override;
	std::int64_t now() noexcept override;
	std::optional<std::string> readModule(std::string_view path) override;

private:
	using Result = Js::HostResult;

	// Runs one engine entry, reports its error and charges its time.
	[[nodiscard]] Js::Outcome run(Fn<Js::Outcome()> entry);
	void settleLater(
		std::uint64_t promiseId,
		bool fulfill,
		nlohmann::json value,
		std::string name = {},
		std::string message = {});

	[[nodiscard]] Result timerSet(const nlohmann::json &args);
	[[nodiscard]] Result subscribe(const nlohmann::json &args, int delta);
	[[nodiscard]] Result ui(
		std::string_view function,
		const nlohmann::json &args,
		std::uint64_t promiseId);
	[[nodiscard]] Result addHandler(
		std::string_view function,
		const nlohmann::json &options);
	[[nodiscard]] Result storage(
		std::string_view function,
		const nlohmann::json &args);
	[[nodiscard]] Result send(
		const nlohmann::json &options,
		std::uint64_t promiseId);
	[[nodiscard]] Result fetch(
		const nlohmann::json &args,
		std::uint64_t promiseId);
	void fetchStep(
		std::uint64_t promiseId,
		QString method,
		QString url,
		std::vector<std::pair<QByteArray, QByteArray>> headers,
		QByteArray body,
		int redirects);
	[[nodiscard]] Result apiCall(
		const nlohmann::json &args,
		std::uint64_t promiseId);

	void loadStorage();
	void scheduleStorageWrite();
	void writeStorage();

	InstalledPlugin _plugin;
	Manifest _manifest;
	QString _root;
	const not_null<InstanceDelegate*> _delegate;
	std::unique_ptr<Js::Engine> _engine;
	bool _stopping = false;
	CpuBudget _budget;
	// fetch and api.call promises not settled yet, at most kMaxInFlight.
	std::set<std::uint64_t> _requests;

	std::map<std::int64_t, std::unique_ptr<QTimer>> _timers;
	std::int64_t _nextTimer = 0;
	std::map<std::string, int> _events;
	std::vector<Handler> _commands;
	std::vector<Handler> _menuItems;
	std::vector<Handler> _hotkeys;
	std::vector<std::int64_t> _settingWatchers;

	std::optional<nlohmann::json> _storage;
	std::unique_ptr<QTimer> _storageTimer;
	std::unique_ptr<QNetworkAccessManager> _network;

};

} // namespace Luxury::Plugins
