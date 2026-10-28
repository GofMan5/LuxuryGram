// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/plugin_hooks_ui.h"

#include "luxury/plugins/plugin_host_api.h"
#include "luxury/plugins/plugin_manager.h"
#include "luxury/utils/telegram_helpers.h"
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

#include <algorithm>

namespace Luxury::Plugins {
namespace {

using json = nlohmann::json;
using Items = const std::vector<Instance::Handler> &(Instance::*)() const;

// Running plugins, or nothing: the one check every hook starts with.
[[nodiscard]] Manager *Active() {
	const auto manager = Manager::Maybe();
	return (manager && manager->active()) ? manager : nullptr;
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

// A chat plugins may see: of the active account, never the service chat.
[[nodiscard]] bool Shown(PeerData *peer) {
	return peer
		&& (peer->id != PeerData::kServiceNotificationsId)
		&& (Core::App().domain().active().maybeSession()
			== &peer->session());
}

[[nodiscard]] bool HasItems(PeerData *peer, Items items) {
	const auto manager = Active();
	if (!manager || !Shown(peer)) {
		return false;
	}
	for (const auto &instance : manager->instances()) {
		if (instance->plugin().granted.has(Permission::Ui)
			&& !(instance->*items)().empty()) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] rpl::event_stream<> &HeaderStream() {
	static auto result = rpl::event_stream<>();
	return result;
}

// One action per item; the click goes to the plugin on the next event loop
// turn, and only if the plugin still runs and still has the item.
void FillItems(
		not_null<Ui::PopupMenu*> menu,
		not_null<PeerData*> peer,
		Items items) {
	const auto manager = Active();
	if (!manager) {
		return;
	}
	const auto session = base::make_weak(&peer->session());
	const auto peerId = peer->id;
	for (const auto &instance : manager->instances()) {
		if (!instance->plugin().granted.has(Permission::Ui)) {
			continue;
		}
		const auto pluginId = QString::fromStdString(instance->plugin().id);
		for (const auto &item : (instance->*items)()) {
			const auto fn = item.fn;
			menu->addAction(item.title, [=] {
				const auto running = Active();
				const auto strong = session.get();
				const auto target = running
					? Find(running, pluginId)
					: nullptr;
				const auto chat = strong
					? strong->data().peerLoaded(peerId)
					: nullptr;
				if (!target || !Shown(chat)) {
					return;
				}
				const auto &list = (target->*items)();
				const auto alive = std::any_of(
					list.begin(),
					list.end(),
					[&](const Instance::Handler &handler) {
						return (handler.fn == fn);
					});
				if (alive) {
					target->callLater(fn, {
						{ "peer", PeerJson(chat) },
					}, true);
				}
			});
		}
	}
}

} // namespace

void FillChatMenu(PeerData *peer, const Ui::Menu::MenuCallback &addAction) {
	if (!HasItems(peer, &Instance::chatMenuItems)) {
		return;
	}
	addAction({
		.text = tr::lng_luxury_plugins_ui_title(tr::now),
		.handler = nullptr,
		.icon = &st::menuIconCustomize,
		.fillSubmenu = [=](not_null<Ui::PopupMenu*> menu) {
			FillItems(menu, peer, &Instance::chatMenuItems);
		},
	});
}

bool HasHeaderButtons(PeerData *peer) {
	return HasItems(peer, &Instance::headerButtons);
}

void FillHeaderMenu(not_null<Ui::PopupMenu*> menu, not_null<PeerData*> peer) {
	if (Shown(peer)) {
		FillItems(menu, peer, &Instance::headerButtons);
	}
}

rpl::producer<> HeaderButtonsChanges() {
	return HeaderStream().events();
}

void NotifyHeaderButtons() {
	HeaderStream().fire({});
}

void FillComposeMenu(not_null<Ui::PopupMenu*> menu, not_null<PeerData*> peer) {
	if (!HasItems(peer, &Instance::composeMenuItems)) {
		return;
	}
	auto submenu = std::make_unique<Ui::PopupMenu>(menu, menu->st());
	FillItems(submenu.get(), peer, &Instance::composeMenuItems);
	menu->addSeparator();
	menu->addAction(
		tr::lng_luxury_plugins_ui_title(tr::now),
		std::move(submenu),
		&st::menuIconCustomize);
}

void ChatOpened(PeerData *peer) {
	const auto manager = Active();
	if (!manager || !Shown(peer)) {
		return;
	}
	for (const auto &instance : manager->instances()) {
		if (instance->wants("chatOpened")) {
			instance->emitLater("chatOpened", { { "peer", PeerJson(peer) } });
		}
	}
}

std::vector<std::pair<QString, QString>> PluginCommands(
		not_null<PeerData*> peer) {
	auto result = std::vector<std::pair<QString, QString>>();
	const auto manager = Active();
	if (!manager || !Shown(peer)) {
		return result;
	}
	for (const auto &command : manager->commands()) {
		result.emplace_back(command.name, command.description);
	}
	return result;
}

QString MessageDecoration(not_null<const HistoryItem*> item) {
	const auto manager = Active();
	const auto peer = item->history()->peer;
	if (!manager || !Shown(peer)) {
		return QString();
	}
	const auto dialogId = getDialogIdFromPeer(peer);
	for (const auto &instance : manager->instances()) {
		if (instance->plugin().granted.has(Permission::Ui)) {
			auto label = instance->decoration(dialogId, item->id.bare);
			if (!label.isEmpty()) {
				return label;
			}
		}
	}
	return QString();
}

} // namespace Luxury::Plugins
