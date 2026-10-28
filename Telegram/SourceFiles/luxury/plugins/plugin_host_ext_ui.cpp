// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
//
// UI extension points of Instance: chat menu, header and compose menu
// items, forms, chat navigation and message labels. Reached from
// Instance::call through IsUiExtFunction.
#include "luxury/plugins/plugin_host_api.h"

#include "luxury/plugins/plugin_hooks_ui.h"
#include "luxury/plugins/plugin_host_logic.h"
#include "luxury/plugins/plugin_ui_logic.h"
#include "luxury/utils/telegram_helpers.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/local_url_handlers.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_key.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"

#include <algorithm>

namespace Luxury::Plugins {
namespace {

using json = nlohmann::json;

[[nodiscard]] Js::HostResult TypeError(std::string text) {
	return Js::HostResult::Error("TypeError", std::move(text));
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

[[nodiscard]] Main::Session *ActiveSession() {
	return Core::IsAppLaunched()
		? Core::App().domain().active().maybeSession()
		: nullptr;
}

[[nodiscard]] Window::Controller *ActiveWindow() {
	return Core::IsAppLaunched() ? Core::App().activeWindow() : nullptr;
}

// The window of the active account, null for another account's window.
[[nodiscard]] Window::SessionController *ActiveController(
		not_null<Main::Session*> session) {
	const auto window = ActiveWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	return (controller && &controller->session() == session)
		? controller
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

// As in plugin_host_api.cpp: the active account only, never the service
// chat.
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

[[nodiscard]] json PeerJson(not_null<PeerData*> peer) {
	return {
		{ "id", std::to_string(getDialogIdFromPeer(peer)) },
		{ "type", peer->isUser()
			? "user"
			: peer->isChannel()
			? "channel"
			: "chat" },
		{ "title", peer->name().toStdString() },
	};
}

// Relayout of a loaded message, so its label is read again.
void ResizeMessage(
		not_null<Main::Session*> session,
		std::int64_t dialogId,
		std::int64_t msgId) {
	if (const auto peer = ResolvePeer(session, dialogId)) {
		if (const auto item = session->data().message(peer->id, MsgId(msgId))) {
			session->data().requestItemResize(item);
		}
	}
}

} // namespace

QString Instance::decoration(std::int64_t dialogId, std::int64_t msgId) const {
	const auto i = _decorations.find({ dialogId, msgId });
	return (i != _decorations.end()) ? i->second : QString();
}

Js::HostResult Instance::uiExt(
		std::string_view function,
		const json &args,
		std::uint64_t promiseId) {
	const auto &options = Arg(args, 0);
	if (function == "ui.addChatMenuItem"
		|| function == "ui.addHeaderButton"
		|| function == "ui.addComposeMenuItem") {
		const auto fn = FunctionId(options.is_object()
			? options.value("handler", json())
			: json());
		if (!fn) {
			return TypeError(
				std::string(function) + ": handler must be a function");
		}
		// A rejected item is released, as in addHandler.
		auto stored = false;
		const auto release = gsl::finally([&] {
			if (!stored) {
				_engine->releaseFunction(*fn);
			}
		});
		auto handler = Handler{
			.title = OptionText(options, "title").trimmed(),
			.fn = *fn,
		};
		if (handler.title.isEmpty()) {
			return TypeError(std::string(function) + ": title is empty");
		}
		auto &list = (function == "ui.addChatMenuItem")
			? _chatMenuItems
			: (function == "ui.addHeaderButton")
			? _headerButtons
			: _composeMenuItems;
		const auto same = std::find_if(list.begin(), list.end(), [&](
				const Handler &existing) {
			return (existing.title == handler.title);
		});
		if (same != list.end()) {
			_engine->releaseFunction(same->fn);
			*same = std::move(handler);
		} else if (list.size() >= kMaxUiItems) {
			return Result::Error("RangeError", "too many items");
		} else {
			list.push_back(std::move(handler));
		}
		stored = true;
		if (&list == &_headerButtons) {
			NotifyHeaderButtons();
		}
		return Result::Value(nullptr);
	} else if (function == "ui.decorateMessage"
		|| function == "ui.refreshMessage") {
		const auto session = ActiveSession();
		if (!session) {
			return Result::Error("Error", "no active Telegram account");
		} else if (!options.is_object()) {
			return TypeError(std::string(function) + ": options must be an object");
		}
		const auto dialogId = DialogId(options.value("peer", json()));
		const auto peer = dialogId ? ResolvePeer(session, *dialogId) : nullptr;
		const auto msgId = DialogId(options.value("id", json()));
		if (!peer) {
			return Result::Error("Error", "unknown peer");
		} else if (!msgId || *msgId <= 0) {
			return TypeError(std::string(function) + ": id must be a message id");
		}
		const auto key = std::make_pair(getDialogIdFromPeer(peer), *msgId);
		if (function == "ui.decorateMessage") {
			const auto text = options.value("text", json());
			if (!text.is_string() && !text.is_null()) {
				return TypeError("ui.decorateMessage: text must be a string");
			}
			const auto label = QString::fromStdString(DecorationText(
				text.is_string() ? text.get<std::string>() : std::string()));
			if (label.isEmpty()) {
				_decorations.erase(key);
			} else if (_decorations.contains(key)
				|| _decorations.size() < kMaxDecorations) {
				_decorations[key] = label;
			} else {
				return Result::Error("RangeError", "too many decorated messages");
			}
		}
		ResizeMessage(session, key.first, key.second);
		return Result::Value(nullptr);
	} else if (Core::IsAppLaunched() && Core::App().passcodeLocked()) {
		return Result::Error("Error", "the app is locked with a passcode");
	} else if (function == "ui.showForm") {
		return showForm(options, promiseId);
	}
	const auto session = ActiveSession();
	if (!session) {
		return Result::Error("Error", "no active Telegram account");
	}
	const auto controller = ActiveController(session);
	if (function == "ui.currentChat") {
		const auto peer = controller
			? controller->activeChatCurrent().peer()
			: nullptr;
		return Result::Value((peer
				&& peer->id != PeerData::kServiceNotificationsId)
			? PeerJson(peer)
			: json());
	} else if (!controller) {
		return Result::Error("Error", "no window of the active account");
	} else if (function == "ui.openChat") {
		const auto dialogId = DialogId(options);
		const auto peer = dialogId ? ResolvePeer(session, *dialogId) : nullptr;
		if (!peer) {
			return Result::Error("Error", "unknown peer");
		}
		controller->showPeerHistory(peer);
		return Result::Value(nullptr);
	} else if (function == "ui.openUrl") {
		if (!options.is_string()
			|| !IsPluginOpenableUrl(options.get_ref<const std::string&>())) {
			return TypeError("ui.openUrl: url must be an http(s) link");
		}
		// t.me and the like become tg:// links that act inside the client
		// (a bot /start, a draft, settings) without the link box.
		const auto url = QString::fromStdString(options.get<std::string>());
		if (Core::TryConvertUrlToLocal(url) != url) {
			return TypeError(
				"ui.openUrl: Telegram links are not allowed; use ui.openChat");
		}
		// The link box can be turned off for web links, so a link opens
		// only on the user's action, as the clipboard does.
		const auto now = crl::now();
		if (!GestureActive(now, _gestureUntil)) {
			return Result::Error(
				"NotAllowedError",
				"ui.openUrl only works right after the user runs a command,"
					" menu item or hotkey of the plugin");
		} else if (!ToastAllowed(now, _lastOpenUrl)) {
			return Result::Error("RangeError", "at most one link a second");
		}
		_lastOpenUrl = now;
		// The client's link path: the box that shows where the link goes,
		// never a direct QDesktopServices hand-off.
		HiddenUrlClickHandler::Open(
			url,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(controller),
				.ignoreIv = true,
				.forceExternalUrlConfirmation = true,
			}));
		return Result::Value(nullptr);
	}
	return TypeError("unknown host function " + std::string(function));
}

Js::HostResult Instance::showForm(const json &schema, std::uint64_t promiseId) {
	auto error = std::string();
	const auto parsed = ParseFormSchema(schema, error);
	if (!parsed) {
		return TypeError("ui.showForm: " + error);
	} else if (_dialog) {
		return Result::Error("Error", "a dialog of the plugin is open");
	}
	const auto window = ActiveWindow();
	if (!window) {
		return Result::Error("Error", "no window to show the dialog in");
	}
	// The title names the plugin: a form must not pass for the app's.
	const auto name = QString::fromStdString(_plugin.name.empty()
		? _plugin.id
		: _plugin.name);
	const auto custom = OptionText(schema, "title");
	const auto title = custom.isEmpty() ? name : (name + u": "_q + custom);
	const auto fields = *parsed;
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
	auto box = Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		auto values = std::vector<std::pair<std::string, Fn<json()>>>();
		Ui::InputField *first = nullptr;
		for (const auto &field : fields) {
			const auto label = QString::fromStdString(field.label);
			if (field.type == "toggle") {
				const auto check = box->addRow(object_ptr<Ui::Checkbox>(
					box,
					label.isEmpty() ? QString::fromStdString(field.key) : label,
					field.value.get<bool>(),
					st::defaultBoxCheckbox));
				values.emplace_back(field.key, [=] {
					return json(check->checked());
				});
				box->addSkip(st::boxLittleSkip);
				continue;
			}
			if (!label.isEmpty()) {
				box->addRow(object_ptr<Ui::FlatLabel>(box, label, st::boxLabel));
				box->addSkip(st::boxLittleSkip);
			}
			if (field.type == "text") {
				const auto input = box->addRow(object_ptr<Ui::InputField>(
					box,
					st::defaultInputField,
					rpl::single(QString::fromStdString(field.placeholder)),
					QString::fromStdString(
						field.value.get_ref<const std::string&>())));
				if (!first) {
					first = input;
				}
				values.emplace_back(field.key, [=] {
					return json(input->getLastText().toStdString());
				});
			} else {
				const auto &options = field.options;
				const auto selected = std::find(
					options.begin(),
					options.end(),
					field.value.get_ref<const std::string&>());
				const auto group = std::make_shared<Ui::RadiobuttonGroup>(
					int(selected - options.begin()));
				for (auto i = 0; i != int(options.size()); ++i) {
					box->addRow(object_ptr<Ui::Radiobutton>(
						box,
						group,
						i,
						QString::fromStdString(options[i]),
						st::defaultBoxCheckbox));
					box->addSkip(st::boxLittleSkip);
				}
				values.emplace_back(field.key, [=] {
					return json(options[group->current()]);
				});
			}
			box->addSkip(st::boxLittleSkip);
		}
		if (first) {
			box->setFocusCallback([=] { first->setFocusFast(); });
		}
		const auto submit = [=] {
			auto result = json::object();
			for (const auto &[key, value] : values) {
				result[key] = value();
			}
			settle(std::move(result));
			box->closeBox();
		};
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

void Instance::clearUiExt() {
	for (const auto list : { &_chatMenuItems, &_headerButtons, &_composeMenuItems }) {
		for (const auto &item : base::take(*list)) {
			_engine->releaseFunction(item.fn);
		}
	}
	const auto decorations = base::take(_decorations);
	if (const auto session = ActiveSession()) {
		for (const auto &[key, label] : decorations) {
			ResizeMessage(session, key.first, key.second);
		}
	}
	NotifyHeaderButtons();
}

} // namespace Luxury::Plugins
