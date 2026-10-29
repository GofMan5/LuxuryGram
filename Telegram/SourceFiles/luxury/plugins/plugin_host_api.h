// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

#include "luxury/plugins/core/plugin_manifest.h"
#include "luxury/plugins/core/plugin_registry.h"
#include "luxury/plugins/js/js_engine.h"
#include "luxury/plugins/plugin_host_logic.h"
#include "base/weak_ptr.h"
#include "base/weak_qptr.h"
#include "rpl/lifetime.h"

#include <QString>
#include <QUrl>

#include <map>
#include <memory>
#include <set>
#include <vector>

class PeerData;
class QNetworkAccessManager;
class QTimer;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Luxury::Plugins {
namespace Tl {
class Schema;
} // namespace Tl

// gzip_packed data of api.call results and updates, capped at 32 MiB.
[[nodiscard]] std::optional<std::string> Gunzip(std::string_view packed);
// The embedded api.tl, parsed once; null with error when it is broken.
[[nodiscard]] const Tl::Schema *ApiSchema(std::string &error);

// Plugins see the active account only: null before launch and login.
[[nodiscard]] Main::Session *ActiveSession();
// A dialog id as a plugin passes it: a number or a decimal string.
[[nodiscard]] std::optional<std::int64_t> DialogId(const nlohmann::json &value);
// A dialog id of the active account only, never another account's peer
// and never the service chat. An id above 48 bits would be masked into
// another peer's bare id (2^48 + 777000 into 777000), so it resolves to
// nothing.
[[nodiscard]] PeerData *ResolvePeer(
	not_null<Main::Session*> session,
	std::int64_t dialogId);

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

	// On the next event loop turn. emitLater queues at most
	// kMaxQueuedEvents, dropping the oldest; a gesture call (a command,
	// menu item or hotkey the user triggered) opens the clipboard for
	// kGestureMs.
	void emitLater(std::string event, nlohmann::json payload);
	void callLater(std::int64_t fn, nlohmann::json args, bool gesture = false);
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

	// Runs one engine entry, reports its error and charges its time, at
	// least the budget's minimum charge unless minimum is false.
	[[nodiscard]] Js::Outcome run(Fn<Js::Outcome()> entry, bool minimum = true);
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
		int redirects,
		bool binary);
	// fetchStep once the address is known to be public.
	void fetchSend(
		std::uint64_t promiseId,
		QString method,
		QUrl url,
		std::vector<std::pair<QByteArray, QByteArray>> headers,
		QByteArray body,
		int redirects,
		bool binary);
	// The active account changed: its requests and events are void.
	void accountChanged();
	[[nodiscard]] Result apiCall(
		const nlohmann::json &args,
		std::uint64_t promiseId);
	// lux.chats, lux.messages beyond send, lux.account, settings.set and
	// ui.notify, see plugin_host_ext_data.cpp.
	[[nodiscard]] Result data(
		std::string_view function,
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

	EventQueue _queue;
	std::int64_t _gestureUntil = 0; // crl::now() ms.
	std::int64_t _lastToast = 0;
	std::int64_t _lastMediaSend = 0; // messages.sendFile and forward.
	// One ui.alert/confirm/prompt at a time, closed on unload.
	base::weak_qptr<Ui::BoxContent> _dialog;
	rpl::lifetime _lifetime;

};

} // namespace Luxury::Plugins
