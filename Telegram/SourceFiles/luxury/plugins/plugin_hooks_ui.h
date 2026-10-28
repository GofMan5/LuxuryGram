// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

// Client call sites of the plugin UI extension points (lux.ui.addChatMenuItem
// and the rest). Each returns at once when no plugin runs; none of them
// enters a plugin's engine synchronously, see docs/plugins/ARCHITECTURE.md
// "Client hooks".

#include "ui/widgets/menu/menu_add_action_callback.h"

class HistoryItem;
class PeerData;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Luxury::Plugins {

// Window::FillDialogsEntryMenu: the "Plugins" submenu of a chat.
void FillChatMenu(PeerData *peer, const Ui::Menu::MenuCallback &addAction);
// TopBarWidget: whether the plugins button shows for the chat, and its menu.
[[nodiscard]] bool HasHeaderButtons(PeerData *peer);
void FillHeaderMenu(not_null<Ui::PopupMenu*> menu, not_null<PeerData*> peer);
// Fires when a plugin adds or drops a header button, or unloads.
[[nodiscard]] rpl::producer<> HeaderButtonsChanges();
void NotifyHeaderButtons();
// The message field's context menu: the "Plugins" submenu for the chat
// the field writes to.
void FillComposeMenu(not_null<Ui::PopupMenu*> menu, not_null<PeerData*> peer);
// SessionController of a main window, its active chat's peer changed: the
// 'chatOpened' event, nothing when no chat.
void ChatOpened(PeerData *peer);
// FieldAutocomplete: /commands of running plugins, as (name, description).
[[nodiscard]] std::vector<std::pair<QString, QString>> PluginCommands(
	not_null<PeerData*> peer);
// The label a plugin put on the message with ui.decorateMessage. Layout and
// paint call it: a cache read that never enters a plugin's engine.
[[nodiscard]] QString MessageDecoration(not_null<const HistoryItem*> item);

} // namespace Luxury::Plugins
