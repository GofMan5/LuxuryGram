// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/plugin_host_api.h"

#include "luxury/plugins/plugin_host_logic.h"
#include "luxury/plugins/plugin_ui_logic.h"
#include "luxury/plugins/core/tl_codec.h"
#include "luxury/utils/telegram_helpers.h"
#include "api/api_common.h"
#include "apiwrap.h"
#include "core/application.h"
#include "core/version.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_chat_participant_status.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtp_instance.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_controller.h"
#include "styles/style_layers.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QKeySequence>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QHostInfo>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <chrono>

namespace Luxury::Plugins {
namespace {

using json = nlohmann::json;

constexpr auto kStack = std::size_t(256) * 1024;
constexpr auto kFetchTimeout = 30000;
constexpr auto kStorageWriteDelay = 1000;
constexpr auto kMaxHandlers = 100;

[[nodiscard]] Js::HostResult TooManyRequests() {
	return Js::HostResult::Error(
		"RangeError",
		"too many requests in flight, at most "
			+ std::to_string(kMaxInFlight));
}

[[nodiscard]] Js::HostResult PermissionError(Permission permission) {
	return Js::HostResult::Error(
		"PermissionError",
		'"' + std::string(PermissionId(permission)) + "\" was not granted");
}

[[nodiscard]] Js::HostResult TypeError(std::string text) {
	return Js::HostResult::Error("TypeError", std::move(text));
}

[[nodiscard]] std::optional<Permission> Needed(std::string_view function) {
	const auto starts = [&](std::string_view prefix) {
		return function.substr(0, prefix.size()) == prefix;
	};
	if (function == "messages.send") {
		return Permission::MessagesSend;
	} else if (starts("ui.")) {
		return Permission::Ui;
	} else if (function == "http.fetch") {
		return Permission::Network;
	} else if (starts("storage.")) {
		return Permission::Storage;
	} else if (starts("clipboard.")) {
		return Permission::Clipboard;
	} else if (function == "api.call") {
		return Permission::TelegramApi;
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<Permission> EventPermission(
		std::string_view event) {
	if (event == "message" || event == "messageOut") {
		return Permission::MessagesRead;
	} else if (event == "beforeSend") {
		return Permission::MessagesModify;
	} else if (event == "update") {
		return Permission::TelegramUpdates;
	} else if (event == "chatOpened") {
		return Permission::Ui;
	}
	return std::nullopt;
}

[[nodiscard]] const json &Arg(const json &args, std::size_t index) {
	static const auto null = json();
	return (args.is_array() && index < args.size()) ? args[index] : null;
}

[[nodiscard]] std::optional<std::int64_t> FunctionId(const json &value) {
	if (value.is_object()) {
		const auto i = value.find("$fn");
		if (i != value.end() && i->is_number_integer()) {
			return i->get<std::int64_t>();
		}
	}
	return std::nullopt;
}

[[nodiscard]] QString OptionText(const json &value, const char *key) {
	if (!value.is_object()) {
		return QString();
	}
	const auto i = value.find(key);
	return (i != value.end() && i->is_string())
		? QString::fromStdString(i->get<std::string>())
		: QString();
}

// Every address of a fetch host must pass, see IsPublicAddress.
[[nodiscard]] bool IsPublic(const QHostAddress &address) {
	const auto ipv6 = address.toIPv6Address();
	auto bytes = std::array<std::uint8_t, 16>();
	std::copy(std::begin(ipv6.c), std::end(ipv6.c), bytes.begin());
	return IsPublicAddress(bytes);
}

[[nodiscard]] bool SameOrigin(const QUrl &a, const QUrl &b) {
	const auto port = [](const QUrl &url) {
		return url.port((url.scheme().toLower() == u"https"_q) ? 443 : 80);
	};
	return (a.scheme().toLower() == b.scheme().toLower())
		&& (a.host().toLower() == b.host().toLower())
		&& (port(a) == port(b));
}

[[nodiscard]] Main::Session *ActiveSession() {
	return Core::IsAppLaunched()
		? Core::App().domain().active().maybeSession()
		: nullptr;
}

[[nodiscard]] std::optional<std::int64_t> DialogId(const json &value) {
	if (value.is_number_integer()) {
		return value.get<std::int64_t>();
	} else if (!value.is_string()) {
		return std::nullopt;
	}
	auto ok = false;
	const auto result = QString::fromStdString(
		value.get<std::string>()).toLongLong(&ok);
	return ok ? std::make_optional<std::int64_t>(result) : std::nullopt;
}

// A dialog id of the active account only, never another account's peer
// and never the service chat. An id above 48 bits would be masked into
// another peer's bare id (2^48 + 777000 into 777000), so it resolves to
// nothing.
[[nodiscard]] PeerData *ResolvePeer(
		not_null<Main::Session*> session,
		std::int64_t dialogId) {
	if (!IsBareDialogId(dialogId)) {
		return nullptr;
	}
	const auto bare = getBareDialogId(dialogId);
	auto &data = session->data();
	PeerData *peer = nullptr;
	if (dialogId > 0) {
		peer = data.userLoaded(UserId(bare));
	} else if (const auto channel = data.channelLoaded(ChannelId(bare))) {
		peer = channel;
	} else {
		peer = data.chatLoaded(ChatId(bare));
	}
	return (peer && peer->id == PeerData::kServiceNotificationsId)
		? nullptr
		: peer;
}

} // namespace

std::optional<std::string> Gunzip(std::string_view packed) {
	auto stream = z_stream();
	if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
		return std::nullopt;
	}
	auto result = std::string();
	auto chunk = std::array<char, 64 * 1024>();
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(packed.data()));
	stream.avail_in = uInt(packed.size());
	auto status = Z_OK;
	while (status == Z_OK) {
		stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
		stream.avail_out = uInt(chunk.size());
		status = inflate(&stream, Z_NO_FLUSH);
		if (status != Z_OK && status != Z_STREAM_END) {
			break;
		}
		result.append(chunk.data(), chunk.size() - stream.avail_out);
		if (result.size() > kInflateLimit) {
			status = Z_MEM_ERROR;
		} else if (status == Z_OK && !stream.avail_in && stream.avail_out) {
			status = Z_DATA_ERROR; // Truncated input.
		}
	}
	inflateEnd(&stream);
	return (status == Z_STREAM_END)
		? std::make_optional(std::move(result))
		: std::nullopt;
}

const Tl::Schema *ApiSchema(std::string &error) {
	static auto parsed = [] {
		auto file = QFile(u":/gui/luxury/plugins/api.tl"_q);
		if (!file.open(QIODevice::ReadOnly)) {
			return Tl::Result<Tl::Schema>::Fail("api.tl is not embedded");
		}
		return Tl::Schema::Parse(file.readAll().toStdString());
	}();
	error = parsed.error;
	return parsed ? &*parsed.value : nullptr;
}

namespace {

[[nodiscard]] Window::Controller *ActiveWindow() {
	return Core::IsAppLaunched() ? Core::App().activeWindow() : nullptr;
}

} // namespace

Instance::Instance(
	InstalledPlugin plugin,
	Manifest manifest,
	QString root,
	not_null<InstanceDelegate*> delegate)
: _plugin(std::move(plugin))
, _manifest(std::move(manifest))
, _root(std::move(root))
, _delegate(delegate) {
}

Instance::~Instance() {
	// Members die before has_weak_ptr: reply callbacks fired while
	// _network is destroyed must already see this instance as gone.
	invalidate_weak_ptrs(this);
	_stopping = true;
	if (const auto dialog = _dialog.get()) {
		dialog->closeBox();
	}
	if (_storageTimer && _storageTimer->isActive()) {
		writeStorage();
	}
	_timers.clear();
	_engine = nullptr;
}

bool Instance::start() {
	auto limits = Js::Limits();
	limits.stack = kStack;
	_engine = std::make_unique<Js::Engine>(limits, *this);
	if (Core::IsAppLaunched()) {
		Core::App().domain().activeSessionChanges(
		) | rpl::on_next([=](Main::Session *) {
			accountChanged();
		}, _lifetime);
	}
	return run([&] { return _engine->loadMain(_manifest.main); }).ok();
}

void Instance::unload() {
	if (_stopping) {
		return;
	}
	if (wants("unload")) {
		[[maybe_unused]] const auto out = run([&] {
			return _engine->emit("unload", {});
		});
	}
	_stopping = true;
	_queue.clear();
	if (const auto dialog = _dialog.get()) {
		dialog->closeBox();
	}
	for (const auto fn : base::take(_settingWatchers)) {
		_engine->releaseFunction(fn);
	}
	clearUiExt();
	if (_storageTimer && _storageTimer->isActive()) {
		writeStorage();
	}
	for (auto &[id, timer] : _timers) {
		timer->stop();
	}
}

bool Instance::wants(const std::string &event) const {
	return !_stopping && _events.contains(event);
}

// Every entry into the engine goes through here, so CpuBudget sees all
// the JS time the plugin takes on the main thread.
Js::Outcome Instance::run(Fn<Js::Outcome()> entry, bool minimum) {
	const auto clock = [] {
		return std::int64_t(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	};
	const auto started = clock();
	auto outcome = entry();
	const auto finished = clock();
	const auto id = QString::fromStdString(_plugin.id);
	if (!outcome.ok()) {
		_delegate->instanceError(id, QString::fromStdString(outcome.error));
	}
	if (!_stopping
		&& _budget.record(
			finished,
			finished - started,
			outcome.overrun,
			minimum)) {
		_stopping = true;
		_delegate->instanceStop(id, u"The plugin used too much CPU time."_q);
	}
	return outcome;
}

void Instance::emitLater(std::string event, json payload) {
	if (_stopping || !_queue.push(std::move(event), std::move(payload))) {
		return; // A drain is scheduled already.
	}
	crl::on_main(this, [=] {
		const auto dropped = _queue.dropped;
		const auto events = _queue.take();
		if (dropped) {
			log(
				Js::LogLevel::Warn,
				std::to_string(dropped)
					+ " events were dropped, the plugin is too slow");
		}
		// One drain pays the minimum charge once, on its first event.
		auto first = true;
		for (const auto &entry : events) {
			if (_stopping) {
				break;
			}
			[[maybe_unused]] const auto out = run([&] {
				return _engine->emit(entry.first, entry.second);
			}, std::exchange(first, false));
		}
	});
}

void Instance::callLater(std::int64_t fn, json args, bool gesture) {
	crl::on_main(this, [=] {
		if (!_stopping) {
			if (gesture) {
				_gestureUntil = crl::now() + kGestureMs;
			}
			[[maybe_unused]] const auto out = run([&] {
				return _engine->callFunction(fn, args);
			});
		}
	});
}

void Instance::settleLater(
		std::uint64_t promiseId,
		bool fulfill,
		json value,
		std::string name,
		std::string message) {
	_requests.erase(promiseId);
	crl::on_main(this, [=] {
		if (_stopping) {
			return;
		}
		[[maybe_unused]] const auto out = run([&] {
			return fulfill
				? _engine->resolve(promiseId, value)
				: _engine->reject(promiseId, name, message);
		});
	});
}

void Instance::accountChanged() {
	// Replies of the old account are dropped: every reply callback checks
	// that its promise is still in _requests.
	_queue.clear();
	_decorations.clear();
	for (const auto promiseId : base::take(_requests)) {
		settleLater(
			promiseId,
			false,
			nullptr,
			"Error",
			"the active account changed");
	}
}

void Instance::settingChanged(const QString &key, const json &value) {
	for (const auto fn : _settingWatchers) {
		callLater(fn, json{ { "key", key.toStdString() }, { "value", value } });
	}
}

json Instance::beforeSend(const json &payload) {
	if (!wants("beforeSend")) {
		return nullptr;
	}
	const auto out = run([&] { return _engine->emit("beforeSend", payload); });
	return out.ok() ? out.value : json();
}

void Instance::log(Js::LogLevel level, std::string_view text) noexcept {
	try {
		_delegate->instanceLog(
			QString::fromStdString(_plugin.id),
			level,
			QString::fromUtf8(text.data(), qsizetype(text.size())));
	} catch (...) {
	}
}

std::int64_t Instance::now() noexcept {
	return crl::now();
}

std::optional<std::string> Instance::readModule(std::string_view path) {
	if (!IsSafeRelativePath(path)) {
		return std::nullopt;
	}
	auto file = QFile(_root
		+ u"/code/"_q
		+ QString::fromUtf8(path.data(), qsizetype(path.size())));
	if (file.size() > kMaxPluginBytes || !file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	return file.readAll().toStdString();
}

Js::HostResult Instance::call(
		std::string_view function,
		json args,
		const Js::CallContext &context) {
	if (_stopping) {
		return Result::Error("Error", "the plugin is stopping");
	} else if (const auto needed = Needed(function)) {
		if (!_plugin.granted.has(*needed)) {
			return PermissionError(*needed);
		}
	}
	const auto &first = Arg(args, 0);
	if (function == "timer.set") {
		return timerSet(first);
	} else if (function == "timer.clear") {
		if (first.is_number_integer()) {
			if (const auto i = _timers.find(first.get<std::int64_t>())
				; i != _timers.end()) {
				i->second->stop();
				i->second.release()->deleteLater();
				_timers.erase(i);
			}
		}
		return Result::Value(nullptr);
	} else if (function == "events.subscribe") {
		return subscribe(first, 1);
	} else if (function == "events.unsubscribe") {
		return subscribe(first, -1);
	} else if (function == "plugin.info") {
		return Result::Value({
			{ "id", _plugin.id },
			{ "version", _plugin.version },
			{ "previousVersion", _plugin.previousVersion },
			{ "reason", _plugin.loadReason },
			{ "apiVersion", kPluginApiVersion },
			{ "clientVersion", LuxuryVersionStr },
			{ "granted", _plugin.granted.ids() },
			{ "lang", Lang::LanguageIdOrDefault(Lang::Id()).toStdString() },
		});
	} else if (function == "settings.get") {
		if (!first.is_string()) {
			return TypeError("settings.get: key must be a string");
		}
		return Result::Value(_delegate->instanceSetting(
			QString::fromStdString(_plugin.id),
			QString::fromStdString(first.get<std::string>())));
	} else if (function == "settings.onChange") {
		const auto fn = FunctionId(first);
		if (!fn) {
			return TypeError("settings.onChange: expected a function");
		}
		if (_settingWatchers.size() >= std::size_t(kMaxSettingWatchers)) {
			_engine->releaseFunction(*fn);
			return Result::Error(
				"RangeError",
				"too many settings.onChange handlers");
		}
		_settingWatchers.push_back(*fn);
		return Result::Value(nullptr);
	} else if (function.substr(0, 10) == "clipboard."
		&& !GestureActive(crl::now(), _gestureUntil)) {
		return Result::Error(
			"NotAllowedError",
			"the clipboard is only available right after the user runs"
				" a command, menu item or hotkey of the plugin");
	} else if (function == "clipboard.read") {
		return Result::Value(
			QGuiApplication::clipboard()->text().toStdString());
	} else if (function == "clipboard.write") {
		if (!first.is_string()) {
			return TypeError("clipboard.write: text must be a string");
		}
		QGuiApplication::clipboard()->setText(
			QString::fromStdString(first.get<std::string>()));
		return Result::Value(nullptr);
	} else if (function.substr(0, 8) == "storage.") {
		return storage(function, args);
	} else if (IsUiExtFunction(function)) {
		return uiExt(function, args, context.promiseId);
	} else if (function.substr(0, 3) == "ui.") {
		return ui(function, args, context.promiseId);
	} else if (function == "messages.send") {
		return send(first, context.promiseId);
	} else if (function == "http.fetch") {
		return fetch(args, context.promiseId);
	} else if (function == "api.call") {
		return apiCall(args, context.promiseId);
	}
	return Result::Error(
		"TypeError",
		"unknown host function " + std::string(function));
}

Js::HostResult Instance::timerSet(const json &options) {
	if (_timers.size() >= std::size_t(kMaxTimers)) {
		return Result::Error("RangeError", "too many timers");
	}
	if (!options.is_object()) {
		return TypeError("timer.set: options must be an object");
	}
	const auto delay = options.value("delay", json(0));
	const auto repeat = options.value("repeat", json(false)).is_boolean()
		&& options["repeat"].get<bool>();
	const auto ms = TimerDelay(delay, repeat);
	const auto id = ++_nextTimer;
	auto timer = std::make_unique<QTimer>();
	timer->setSingleShot(!repeat);
	timer->setInterval(ms);
	QObject::connect(timer.get(), &QTimer::timeout, [=] {
		if (_stopping) {
			return;
		}
		if (!repeat) {
			// Deleting a QTimer inside its own timeout is not safe.
			if (const auto i = _timers.find(id); i != _timers.end()) {
				i->second.release()->deleteLater();
				_timers.erase(i);
			}
		}
		[[maybe_unused]] const auto out = run([&] {
			return _engine->fireTimer(id);
		});
	});
	timer->start();
	_timers.emplace(id, std::move(timer));
	return Result::Value(id);
}

Js::HostResult Instance::subscribe(const json &name, int delta) {
	if (!name.is_string()) {
		return TypeError("event name must be a string");
	}
	const auto event = name.get<std::string>();
	if (delta > 0 && !KnownEvent(event)) {
		return TypeError("unknown event \"" + event + '"');
	}
	if (const auto needed = EventPermission(event)) {
		if (delta > 0 && !_plugin.granted.has(*needed)) {
			return PermissionError(*needed);
		}
	}
	if ((_events[event] += delta) <= 0) {
		_events.erase(event);
	}
	return Result::Value(nullptr);
}

Js::HostResult Instance::ui(
		std::string_view function,
		const json &args,
		std::uint64_t promiseId) {
	const auto &options = Arg(args, 0);
	if (function.substr(0, 6) == "ui.add") {
		return addHandler(function, options);
	} else if (Core::IsAppLaunched() && Core::App().passcodeLocked()) {
		return Result::Error("Error", "the app is locked with a passcode");
	} else if (function == "ui.toast") {
		if (!options.is_string()) {
			return TypeError("ui.toast: text must be a string");
		}
		const auto now = crl::now();
		if (!ToastAllowed(now, _lastToast)) {
			return Result::Error("RangeError", "at most one toast a second");
		}
		_lastToast = now;
		Ui::Toast::Show(QString::fromStdString(options.get<std::string>()));
		return Result::Value(nullptr);
	} else if (_dialog) {
		return Result::Error("Error", "a dialog of the plugin is open");
	}
	const auto window = ActiveWindow();
	if (!window) {
		return Result::Error("Error", "no window to show the dialog in");
	}
	// The title names the plugin: a dialog must not pass for the app's.
	const auto name = QString::fromStdString(_plugin.name.empty()
		? _plugin.id
		: _plugin.name);
	const auto custom = OptionText(options, "title");
	const auto title = custom.isEmpty() ? name : (name + u": "_q + custom);
	const auto text = OptionText(options, "text");
	const auto weak = base::make_weak(this);
	const auto settled = std::make_shared<bool>(false);
	const auto settle = [=](json value) {
		if (!*settled) {
			*settled = true;
			if (const auto strong = weak.get()) {
				strong->_dialog = nullptr;
				strong->settleLater(promiseId, true, std::move(value));
			}
		}
	};
	if (function == "ui.alert" || function == "ui.confirm") {
		const auto confirm = (function == "ui.confirm");
		auto box = Ui::MakeConfirmBox({
			.text = text,
			.confirmed = [=](Fn<void()> close) {
				settle(true);
				close();
			},
			.confirmText = tr::lng_box_ok(),
			.title = v::text::data(title),
			.inform = !confirm,
		});
		box->boxClosing() | rpl::on_next([=] {
			settle(!confirm);
		}, box->lifetime());
		_dialog = box.data();
		window->uiShow()->showBox(std::move(box));
		return Result::Pending();
	} else if (function == "ui.prompt") {
		const auto placeholder = OptionText(options, "placeholder");
		const auto value = OptionText(options, "value");
		auto box = Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle(rpl::single(title));
			const auto field = box->addRow(object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				rpl::single(placeholder),
				value));
			box->setFocusCallback([=] { field->setFocusFast(); });
			const auto submit = [=] {
				settle(field->getLastText().toStdString());
				box->closeBox();
			};
			field->submits() | rpl::on_next(submit, field->lifetime());
			box->addButton(tr::lng_box_ok(), submit);
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			box->boxClosing() | rpl::on_next([=] {
				settle(nullptr);
			}, box->lifetime());
		});
		_dialog = box.data();
		window->uiShow()->showBox(std::move(box));
		return Result::Pending();
	}
	return TypeError("unknown host function " + std::string(function));
}

Js::HostResult Instance::addHandler(
		std::string_view function,
		const json &options) {
	const auto fn = FunctionId(options.is_object()
		? options.value("handler", json())
		: json());
	if (!fn) {
		return TypeError(std::string(function) + ": handler must be a function");
	}
	auto handler = Handler{ .fn = *fn };
	// A rejected handler is released: a loop of bad calls must not grow
	// the engine's function table.
	auto stored = false;
	const auto release = gsl::finally([&] {
		if (!stored) {
			_engine->releaseFunction(*fn);
		}
	});
	auto *list = &_commands;
	if (function == "ui.addCommand") {
		auto name = OptionText(options, "name");
		if (name.startsWith('/')) {
			name = name.mid(1);
		}
		if (!IsCommandName(name.toStdString())) {
			return TypeError("ui.addCommand: name must be [a-z0-9_]{1,32}");
		}
		handler.title = name;
		handler.description = OptionText(options, "description");
	} else if (function == "ui.addMessageMenuItem") {
		handler.title = OptionText(options, "title").trimmed();
		if (handler.title.isEmpty()) {
			return TypeError("ui.addMessageMenuItem: title is empty");
		}
		list = &_menuItems;
	} else if (function == "ui.addHotkey") {
		handler.title = OptionText(options, "keys");
		const auto sequence = QKeySequence(
			handler.title,
			QKeySequence::PortableText);
		if (sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown) {
			return TypeError("ui.addHotkey: keys must be one shortcut");
		}
		handler.keys = sequence[0].toCombined();
		if (HotkeyReserved(handler.keys)) {
			return TypeError("ui.addHotkey: this shortcut is reserved");
		}
		list = &_hotkeys;
	} else {
		return TypeError("unknown host function " + std::string(function));
	}
	const auto same = std::find_if(list->begin(), list->end(), [&](
			const Handler &existing) {
		return (list == &_hotkeys)
			? (existing.keys == handler.keys)
			: (existing.title == handler.title);
	});
	if (same != list->end()) {
		_engine->releaseFunction(same->fn);
		*same = std::move(handler);
	} else if (list->size() >= std::size_t(kMaxHandlers)) {
		return Result::Error("RangeError", "too many handlers");
	} else {
		list->push_back(std::move(handler));
	}
	stored = true;
	return Result::Value(nullptr);
}

void Instance::loadStorage() {
	if (_storage) {
		return;
	}
	auto file = QFile(_root + u"/data/storage.json"_q);
	auto parsed = json();
	if (file.size() <= qint64(kStorageQuota)
		&& file.open(QIODevice::ReadOnly)) {
		parsed = json::parse(file.readAll().toStdString(), nullptr, false);
	}
	_storage = parsed.is_object() ? std::move(parsed) : json::object();
}

void Instance::scheduleStorageWrite() {
	if (!_storageTimer) {
		_storageTimer = std::make_unique<QTimer>();
		_storageTimer->setSingleShot(true);
		QObject::connect(_storageTimer.get(), &QTimer::timeout, [=] {
			writeStorage();
		});
	}
	if (!_storageTimer->isActive()) {
		_storageTimer->start(kStorageWriteDelay);
	}
}

void Instance::writeStorage() {
	if (_storageTimer) {
		_storageTimer->stop();
	}
	if (!_storage) {
		return;
	}
	QDir().mkpath(_root + u"/data"_q);
	auto file = QSaveFile(_root + u"/data/storage.json"_q);
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(QByteArray::fromStdString(_storage->dump())) < 0
		|| !file.commit()) {
		log(Js::LogLevel::Error, "storage.json could not be written");
	}
}

Js::HostResult Instance::storage(std::string_view function, const json &args) {
	loadStorage();
	auto &data = *_storage;
	const auto &key = Arg(args, 0);
	if (function == "storage.keys") {
		auto keys = json::array();
		for (const auto &[name, value] : data.items()) {
			keys.push_back(name);
		}
		return Result::Value(std::move(keys));
	} else if (!key.is_string()) {
		return TypeError(std::string(function) + ": key must be a string");
	}
	const auto name = key.get<std::string>();
	if (function == "storage.get") {
		const auto i = data.find(name);
		return Result::Value((i != data.end()) ? *i : json());
	} else if (function == "storage.remove") {
		if (data.erase(name)) {
			scheduleStorageWrite();
		}
		return Result::Value(nullptr);
	} else if (function == "storage.set") {
		auto copy = data;
		copy[name] = Arg(args, 1);
		if (!StorageFits(copy)) {
			return Result::Error(
				"QuotaExceededError",
				"plugin storage is limited to 5 MiB");
		}
		data = std::move(copy);
		scheduleStorageWrite();
		return Result::Value(nullptr);
	}
	return TypeError("unknown host function " + std::string(function));
}

Js::HostResult Instance::send(const json &options, std::uint64_t promiseId) {
	const auto session = ActiveSession();
	if (!InFlightAllowed(int(_requests.size()))) {
		return TooManyRequests();
	} else if (!session) {
		return Result::Error("Error", "no active Telegram account");
	} else if (!options.is_object()) {
		return TypeError("messages.send: options must be an object");
	}
	const auto dialogId = DialogId(options.value("peer", json()));
	const auto peer = dialogId ? ResolvePeer(session, *dialogId) : nullptr;
	const auto text = OptionText(options, "text");
	// ResolvePeer already turns the service chat and its 48-bit aliases
	// into nothing; this names the refusal for the plain id.
	if (IsServiceUserId(options.value("peer", json()))) {
		return Result::Error(
			"PermissionError",
			"the Telegram service chat is never available to plugins");
	} else if (!peer) {
		return Result::Error("Error", "unknown peer");
	} else if (text.trimmed().isEmpty()) {
		return TypeError("messages.send: text is empty");
	} else if (!Data::CanSendTexts(peer)) {
		return Result::Error("Error", "you cannot write to this chat");
	}
	const auto replyTo = DialogId(options.value("replyTo", json()));
	const auto silent = options.value("silent", json()).is_boolean()
		&& options["silent"].get<bool>();
	const auto weak = base::make_weak(this);
	_requests.emplace(promiseId);
	// Later: a send runs the beforeSend chain, this engine included.
	crl::on_main(session, [=] {
		const auto alive = weak.get();
		if (!alive || alive->_stopping || !alive->_requests.contains(promiseId)) {
			return; // Unloaded, stopped or the account changed meanwhile.
		}
		const auto history = session->data().history(peer);
		auto action = Api::SendAction(history);
		action.options.silent = silent;
		if (replyTo && *replyTo > 0) {
			action.replyTo.messageId = FullMsgId(peer->id, MsgId(*replyTo));
		}
		auto message = Api::MessageToSend(std::move(action));
		message.textWithTags = TextWithTags{ text };
		const auto sent = session->api().sendMessage(std::move(message));
		if (const auto strong = weak.get()) {
			strong->settleLater(
				promiseId,
				sent,
				nullptr,
				"Error",
				"a plugin cancelled the message");
		}
	});
	return Result::Pending();
}

Js::HostResult Instance::fetch(const json &args, std::uint64_t promiseId) {
	const auto &url = Arg(args, 0);
	const auto &options = Arg(args, 1);
	if (!InFlightAllowed(int(_requests.size()))) {
		return TooManyRequests();
	} else if (!url.is_string()) {
		return TypeError("http.fetch: url must be a string");
	} else if (!options.is_null() && !options.is_object()) {
		return TypeError("http.fetch: options must be an object");
	}
	auto method = OptionText(options, "method").toUpper();
	if (method.isEmpty()) {
		method = u"GET"_q;
	}
	static const auto methods = QStringList{
		u"GET"_q, u"POST"_q, u"PUT"_q, u"PATCH"_q, u"DELETE"_q, u"HEAD"_q,
	};
	if (!methods.contains(method)) {
		return TypeError("http.fetch: unsupported method");
	}
	auto headers = std::vector<std::pair<QByteArray, QByteArray>>();
	if (options.is_object() && options.contains("headers")) {
		const auto &list = options["headers"];
		if (!list.is_object()) {
			return TypeError("http.fetch: headers must be an object");
		}
		for (const auto &[name, value] : list.items()) {
			if (!value.is_string()) {
				return TypeError("http.fetch: header values must be strings");
			} else if (QByteArray::fromStdString(name).toLower() == "host") {
				continue; // HTTP/1 would send it instead of the URL host.
			}
			headers.emplace_back(
				QByteArray::fromStdString(name),
				QByteArray::fromStdString(value.get<std::string>()));
		}
	}
	if (options.is_object()
		&& options.contains("body")
		&& !options["body"].is_string()) {
		return TypeError("http.fetch: body must be a string");
	}
	const auto body = OptionText(options, "body").toUtf8();
	const auto binary = options.is_object()
		&& (options.value("binary", json()) == json(true));
	// Before fetchStep: it may settle, and so erase, right away.
	_requests.emplace(promiseId);
	fetchStep(
		promiseId,
		method,
		QString::fromStdString(url.get<std::string>()),
		std::move(headers),
		body,
		0,
		binary);
	return Result::Pending();
}

void Instance::fetchStep(
		std::uint64_t promiseId,
		QString method,
		QString url,
		std::vector<std::pair<QByteArray, QByteArray>> headers,
		QByteArray body,
		int redirects,
		bool binary) {
	const auto parsed = QUrl(url, QUrl::StrictMode);
	if (!parsed.isValid()
		|| !FetchAllowed(
			parsed.scheme().toLower().toStdString(),
			parsed.host().toLower().toStdString(),
			parsed.port(),
			_plugin.network)) {
		settleLater(
			promiseId,
			false,
			nullptr,
			"PermissionError",
			"\"" + url.toStdString() + "\" is not in the plugin's network list");
		return;
	}
	const auto refuse = [=](Instance *that) {
		that->settleLater(
			promiseId,
			false,
			nullptr,
			"PermissionError",
			"\"" + url.toStdString() + "\" is a private address");
	};
	if (!_network) {
		_network = std::make_unique<QNetworkAccessManager>();
	}
	auto literal = QHostAddress();
	const auto isLiteral = literal.setAddress(parsed.host());
	// Behind an application proxy the proxy resolves the name, and a local
	// lookup would leak it past the proxy: only an IP literal is checked.
	const auto proxied = (QNetworkProxy::applicationProxy().type()
		!= QNetworkProxy::NoProxy);
	if (isLiteral || proxied) {
		if (isLiteral && !IsPublic(literal)) {
			return refuse(this);
		}
		return fetchSend(
			promiseId,
			method,
			parsed,
			std::move(headers),
			body,
			redirects,
			binary);
	}
	// ponytail: QHostInfo caches this answer for 60 s and the connection
	// reads that cache, so it goes to a checked address; a host rebinding
	// after the entry expires slips through. Pin the address if it matters.
	const auto weak = base::make_weak(this);
	QHostInfo::lookupHost(parsed.host(), _network.get(), [=](
			const QHostInfo &info) {
		const auto strong = weak.get();
		if (!strong
			|| strong->_stopping
			|| !strong->_requests.contains(promiseId)) {
			return;
		}
		const auto addresses = info.addresses();
		if (info.error() != QHostInfo::NoError || addresses.isEmpty()) {
			strong->settleLater(
				promiseId,
				false,
				nullptr,
				"NetworkError",
				info.errorString().toStdString());
			return;
		}
		for (const auto &address : addresses) {
			if (!IsPublic(address)) {
				return refuse(strong);
			}
		}
		strong->fetchSend(
			promiseId,
			method,
			parsed,
			headers,
			body,
			redirects,
			binary);
	});
}

void Instance::fetchSend(
		std::uint64_t promiseId,
		QString method,
		QUrl url,
		std::vector<std::pair<QByteArray, QByteArray>> headers,
		QByteArray body,
		int redirects,
		bool binary) {
	auto request = QNetworkRequest(url);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::ManualRedirectPolicy);
	request.setTransferTimeout(kFetchTimeout);
	request.setHeader(QNetworkRequest::UserAgentHeader, u"LuxuryGram"_q);
	for (const auto &[name, value] : headers) {
		request.setRawHeader(name, value);
	}
	const auto reply = _network->sendCustomRequest(
		request,
		method.toUtf8(),
		body);
	const auto tooBig = std::make_shared<bool>(false);
	QObject::connect(reply, &QNetworkReply::downloadProgress, [=](
			qint64 received,
			qint64) {
		if (received > kFetchLimit) {
			*tooBig = true;
			reply->abort();
		}
	});
	const auto weak = base::make_weak(this);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto strong = weak.get();
		if (!strong
			|| strong->_stopping
			|| !strong->_requests.contains(promiseId)) {
			return;
		}
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto fail = [&](std::string name, std::string text) {
			strong->settleLater(
				promiseId,
				false,
				nullptr,
				std::move(name),
				std::move(text));
		};
		if (*tooBig) {
			return fail("RangeError", "the response is over 10 MiB");
		} else if (status <= 0) {
			return fail("NetworkError", reply->errorString().toStdString());
		}
		const auto location = reply->rawHeader("Location");
		if (status >= 300 && status < 400 && !location.isEmpty()) {
			if (redirects >= kMaxRedirects) {
				return fail("NetworkError", "too many redirects");
			}
			const auto next = reply->url().resolved(
				QUrl::fromEncoded(location));
			const auto get = (status == 303)
				|| ((status == 301 || status == 302) && method == u"POST"_q);
			auto kept = headers;
			if (!SameOrigin(reply->url(), next)) {
				// Credentials stay with the origin they were written for.
				kept.erase(std::remove_if(kept.begin(), kept.end(), [](
						const std::pair<QByteArray, QByteArray> &header) {
					return StripOnCrossOrigin(header.first.toStdString());
				}), kept.end());
			}
			strong->fetchStep(
				promiseId,
				get ? u"GET"_q : method,
				next.toString(QUrl::FullyEncoded),
				std::move(kept),
				get ? QByteArray() : body,
				redirects + 1,
				binary);
			return;
		}
		const auto bytes = reply->read(kFetchLimit + 1);
		if (bytes.size() > kFetchLimit) {
			return fail("RangeError", "the response is over 10 MiB");
		}
		auto names = json::object();
		for (const auto &[name, value] : reply->rawHeaderPairs()) {
			names[QString::fromLatin1(name).toLower().toStdString()]
				= QString::fromLatin1(value).toStdString();
		}
		auto result = json{
			{ "status", status },
			{ "headers", std::move(names) },
		};
		if (binary) {
			result["base64"] = bytes.toBase64().toStdString();
		} else {
			// Invalid UTF-8 becomes U+FFFD, JSON needs valid text.
			result["body"] = QString::fromUtf8(bytes).toStdString();
		}
		strong->settleLater(promiseId, true, std::move(result));
	});
}

Js::HostResult Instance::apiCall(const json &args, std::uint64_t promiseId) {
	const auto session = ActiveSession();
	const auto &name = Arg(args, 0);
	const auto &params = Arg(args, 1);
	if (!InFlightAllowed(int(_requests.size()))) {
		return TooManyRequests();
	} else if (!session) {
		return Result::Error("Error", "no active Telegram account");
	} else if (!name.is_string()) {
		return TypeError("api.call: method must be a string");
	} else if (!params.is_null() && !params.is_object()) {
		return TypeError("api.call: params must be an object");
	}
	const auto method = name.get<std::string>();
	if (IsMethodForbidden(method)) {
		return Result::Error(
			"PermissionError",
			'"' + method + "\" is never allowed to plugins");
	}
	auto error = std::string();
	const auto schema = ApiSchema(error);
	if (!schema) {
		return Result::Error("Error", "the API schema is broken: " + error);
	}
	const auto combinator = schema->method(method);
	if (!combinator) {
		return TypeError("unknown method \"" + method + '"');
	} else if (HasQueryParam(*combinator)) {
		return Result::Error(
			"PermissionError",
			'"' + method + "\" wraps another query and is not allowed");
	} else if (HasPaidParam(params)) {
		return Result::Error(
			"PermissionError",
			'"' + method + "\" spends Stars and is not allowed");
	} else if (MentionsServiceUser(params)) {
		return Result::Error(
			"PermissionError",
			"the Telegram service chat is never available to plugins");
	}
	auto call = params.is_object() ? params : json::object();
	call["_"] = method;
	const auto words = Tl::EncodeMethod(*schema, call);
	const auto type = Tl::CallReturnType(*schema, call);
	if (!words) {
		return TypeError(words.error);
	} else if (words.value->empty() || !type) {
		return TypeError("api.call: cannot encode \"" + method + '"');
	}
	auto request = MTP::details::SerializedRequest::Prepare(
		uint32(words.value->size()));
	for (const auto word : *words.value) {
		request->push_back(mtpPrime(word));
	}
	const auto weak = base::make_weak(this);
	const auto returnType = *type;
	auto done = [=](const MTP::Response &response) {
		const auto strong = weak.get();
		if (!strong || !strong->_requests.contains(promiseId)) {
			return true;
		}
		const auto decoded = Tl::DecodeResult(
			*schema,
			returnType,
			reinterpret_cast<const uint32_t*>(response.reply.constData()),
			std::size_t(response.reply.size()),
			Gunzip);
		auto result = decoded ? *decoded.value : json();
		if (decoded && !StripServiceUser(result)) {
			// Login codes of the service chat, see StripServiceUser.
			strong->settleLater(
				promiseId,
				false,
				nullptr,
				"PermissionError",
				"the result holds the Telegram service chat");
		} else if (decoded) {
			HideOwnPhone(result);
			strong->settleLater(promiseId, true, std::move(result));
		} else {
			strong->settleLater(
				promiseId,
				false,
				nullptr,
				"TypeError",
				"cannot decode the result: " + decoded.error);
		}
		return true;
	};
	auto fail = [=](const MTP::Error &error, const MTP::Response &) {
		const auto strong = weak.get();
		if (strong && strong->_requests.contains(promiseId)) {
			// The engine rejects with an Error: type is the name, the
			// message carries both as {type, code} JSON.
			strong->settleLater(
				promiseId,
				false,
				nullptr,
				error.type().toStdString(),
				json{
					{ "type", error.type().toStdString() },
					{ "code", error.code() },
				}.dump());
		}
		return true;
	};
	_requests.emplace(promiseId);
	session->mtp().sendSerialized(
		MTP::details::GetNextRequestId(),
		std::move(request),
		MTP::ResponseHandler{ std::move(done), std::move(fail) },
		0,
		0,
		0);
	return Result::Pending();
}

} // namespace Luxury::Plugins
