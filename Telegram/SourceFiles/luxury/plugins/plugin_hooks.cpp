// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/plugin_hooks.h"

#include "luxury/plugins/plugin_host_api.h"
#include "luxury/plugins/plugin_host_logic.h"
#include "luxury/plugins/plugin_manager.h"
#include "luxury/plugins/core/tl_codec.h"
#include "luxury/utils/telegram_helpers.h"
#include "api/api_common.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_menu_icons.h"

#include <QApplication>
#include <QKeyEvent>
#include <QWidget>

namespace Luxury::Plugins {
namespace {

using json = nlohmann::json;

static_assert(kKeyEscape == Qt::Key_Escape);
static_assert(kKeyReturn == Qt::Key_Return);
static_assert(kKeyEnter == Qt::Key_Enter);
static_assert(kShiftModifier == int(Qt::ShiftModifier));
static_assert(kControlModifier == int(Qt::ControlModifier));
static_assert(kModifierMask == unsigned(Qt::KeyboardModifierMask));

// Running plugins, or nothing: the one check every hook starts with.
[[nodiscard]] Manager *Active() {
	const auto manager = Manager::Maybe();
	return (manager && manager->active()) ? manager : nullptr;
}

// Plugins act on the active account only (see ActiveSession in
// plugin_host_api.cpp): another account's events never reach them.
[[nodiscard]] bool IsActive(not_null<Main::Session*> session) {
	return Core::App().domain().active().maybeSession() == session;
}

[[nodiscard]] Instance *Find(not_null<Manager*> manager, const QString &id) {
	for (const auto &instance : manager->instances()) {
		if (QString::fromStdString(instance->plugin().id) == id) {
			return instance;
		}
	}
	return nullptr;
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

[[nodiscard]] json MessageJson(not_null<HistoryItem*> item) {
	const auto reply = item->replyToId();
	return {
		{ "id", std::to_string(item->id.bare) },
		{ "peer", PeerJson(item->history()->peer) },
		{ "from", PeerJson(item->from()) },
		{ "date", item->date() },
		{ "text", item->originalText().text.toStdString() },
		{ "out", item->out() },
		{ "replyTo", reply ? json(std::to_string(reply.bare)) : json() },
		{ "hasMedia", item->media() != nullptr },
	};
}

void EmitMessage(not_null<HistoryItem*> item, const std::string &event) {
	const auto manager = Active();
	if (!manager
		|| !item->isRegular()
		|| item->isService()
		|| (item->history()->peer->id == PeerData::kServiceNotificationsId)
		|| !IsActive(&item->history()->session())) {
		return;
	}
	auto payload = std::optional<json>();
	for (const auto &instance : manager->instances()) {
		// subscribe() refuses these events without messages.read.
		if (instance->wants(event)) {
			if (!payload) {
				payload = json{ { "message", MessageJson(item) } };
			}
			instance->emitLater(event, *payload);
		}
	}
}

} // namespace

bool BeforeSend(Api::MessageToSend &message) {
	const auto manager = Active();
	if (!manager || message.textWithTags.text.isEmpty()) {
		return true;
	}
	const auto peer = message.action.history->peer;
	if (!IsActive(&peer->session())) {
		return true;
	}
	auto fold = SendFold{ message.textWithTags.text.toStdString() };
	for (const auto &instance : manager->instances()) {
		if (instance->plugin().granted.has(Permission::MessagesModify)) {
			FoldBeforeSend(fold, instance->beforeSend({
				{ "peer", PeerJson(peer) },
				{ "text", fold.text },
			}));
		}
	}
	if (fold.cancelled) {
		return false;
	} else if (fold.changed) {
		// The entities belong to the old text; a plugin returns plain text.
		message.textWithTags = TextWithTags{
			QString::fromStdString(fold.text),
		};
	}
	return true;
}

bool HasCommands() {
	const auto manager = Active();
	return manager && !manager->commands().empty();
}

bool LocalCommand(not_null<History*> history, const QString &text) {
	const auto manager = Active();
	if (!manager || !text.startsWith('/') || !IsActive(&history->session())) {
		return false;
	}
	const auto parsed = ParseLocalCommand(text.toStdString());
	if (!parsed) {
		return false;
	}
	const auto name = QString::fromStdString(parsed->name);
	for (const auto &instance : manager->instances()) {
		for (const auto &command : instance->commands()) {
			if (command.title == name) {
				instance->callLater(command.fn, {
					{ "peer", PeerJson(history->peer) },
					{ "args", parsed->args },
				}, true);
				return true;
			}
		}
	}
	return false;
}

void IncomingMessage(not_null<HistoryItem*> item) {
	if (!item->out()) {
		EmitMessage(item, "message");
	}
}

void MessageSent(not_null<HistoryItem*> item) {
	EmitMessage(item, "messageOut");
}

namespace {

template <typename Object>
void EmitUpdate(not_null<Main::Session*> session, const Object &updates) {
	const auto manager = Active();
	if (!manager || !IsActive(session)) {
		return;
	}
	auto wanting = std::vector<not_null<Instance*>>();
	for (const auto &instance : manager->instances()) {
		// subscribe() refuses 'update' without telegram.updates.
		if (instance->wants("update")) {
			wanting.push_back(instance);
		}
	}
	if (wanting.empty()) {
		return;
	}
	auto error = std::string();
	const auto schema = ApiSchema(error);
	if (!schema) {
		return;
	}
	auto buffer = mtpBuffer();
	updates.write(buffer);
	const auto decoded = Tl::DecodeObject(
		*schema,
		reinterpret_cast<const uint32_t*>(buffer.constData()),
		std::size_t(buffer.size()),
		Gunzip);
	if (!decoded) {
		// ponytail: an update api.tl cannot read is dropped silently; log
		// it to the plugins if layer drift ever makes this common.
		return;
	}
	auto update = *decoded.value;
	if (!StripServiceUser(update)) {
		return; // A login code of the service chat, see StripServiceUser.
	}
	HideOwnPhone(update);
	const auto payload = json{ { "update", std::move(update) } };
	for (const auto &instance : wanting) {
		instance->emitLater("update", payload);
	}
}

} // namespace

void UpdatesReceived(
		not_null<Main::Session*> session,
		const MTPUpdates &updates) {
	EmitUpdate(session, updates);
}

void UpdatesReceived(
		not_null<Main::Session*> session,
		const MTPupdates_Difference &difference) {
	EmitUpdate(session, difference);
}

void UpdatesReceived(
		not_null<Main::Session*> session,
		const MTPupdates_ChannelDifference &difference) {
	EmitUpdate(session, difference);
}

void FillMessageMenu(not_null<Ui::PopupMenu*> menu, HistoryItem *item) {
	const auto manager = Active();
	if (!manager
		|| !item
		|| !item->isRegular()
		|| (item->history()->peer->id == PeerData::kServiceNotificationsId)
		|| !IsActive(&item->history()->session())) {
		return;
	}
	const auto items = manager->messageMenuItems();
	if (items.empty()) {
		return;
	}
	const auto session = base::make_weak(&item->history()->session());
	const auto itemId = item->fullId();
	auto submenu = std::make_unique<Ui::PopupMenu>(menu, menu->st());
	for (const auto &entry : items) {
		const auto pluginId = entry.pluginId;
		const auto handler = entry.handler;
		submenu->addAction(entry.title, [=] {
			const auto manager = Active();
			const auto strong = session.get();
			const auto instance = manager ? Find(manager, pluginId) : nullptr;
			const auto message = strong
				? strong->data().message(itemId)
				: nullptr;
			if (!instance || !message) {
				return;
			}
			// The whole message only with messages.read.
			const auto full = instance->plugin().granted.has(
				Permission::MessagesRead);
			instance->callLater(handler, { { "message", full
				? MessageJson(message)
				: json{
					{ "id", std::to_string(message->id.bare) },
					{ "peer", PeerJson(message->history()->peer) },
				} } }, true);
		});
	}
	menu->addAction(
		tr::lng_luxury_plugins_ui_title(tr::now),
		std::move(submenu),
		&st::menuIconCustomize);
}

bool HandleHotkey(not_null<QObject*> object, not_null<QKeyEvent*> event) {
	const auto manager = Active();
	if (!manager || Core::App().passcodeLocked()) {
		return false;
	}
	// The filter sees the event on every parent it propagates to: decide
	// once, for the widget that receives it first.
	const auto focus = QApplication::focusWidget();
	const auto receiver = focus ? focus : QApplication::activeWindow();
	if (!receiver || object.get() != receiver) {
		return false;
	}
	const auto modifiers = event->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
	const auto typing = focus
		&& (focus->inherits("QLineEdit")
			|| focus->inherits("QTextEdit")
			|| focus->inherits("QPlainTextEdit"));
	// A text field owns every key, Ctrl+C and Enter included.
	if (typing) {
		return false;
	}
	const auto keys = QKeyCombination(modifiers, Qt::Key(event->key()))
		.toCombined();
	if (HotkeyReserved(keys)) {
		return false;
	}
	for (const auto &instance : manager->instances()) {
		if (!instance->plugin().granted.has(Permission::Ui)) {
			continue;
		}
		for (const auto &hotkey : instance->hotkeys()) {
			if (hotkey.keys == keys) {
				if (!event->isAutoRepeat()) {
					instance->callLater(hotkey.fn, nullptr, true);
				}
				return true;
			}
		}
	}
	return false;
}

} // namespace Luxury::Plugins
