// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

// Client call sites of the plugin system. Each returns at once when no
// plugin runs, see docs/plugins/ARCHITECTURE.md "Client hooks".

class History;
class HistoryItem;
class QKeyEvent;
class QObject;

namespace Api {
struct MessageToSend;
} // namespace Api

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Luxury::Plugins {

// ApiWrap::sendMessage: false when a plugin cancelled the send.
[[nodiscard]] bool BeforeSend(Api::MessageToSend &message);
// HistoryWidget::send, checked before the text is read: a running plugin
// registered a command.
[[nodiscard]] bool HasCommands();
// HistoryWidget::send: true when the text was a plugin's command. Only a
// /name some plugin registered is consumed, before any bot sees it.
[[nodiscard]] bool LocalCommand(not_null<History*> history, const QString &text);
// A new unread incoming message.
void IncomingMessage(not_null<HistoryItem*> item);
// A message of ours got its server id.
void MessageSent(not_null<HistoryItem*> item);
// Updates::applyUpdates (pushes and RPC results) and every difference:
// what session applies, once per seq batch.
void UpdatesReceived(
	not_null<Main::Session*> session,
	const MTPUpdates &updates);
void UpdatesReceived(
	not_null<Main::Session*> session,
	const MTPupdates_Difference &difference);
void UpdatesReceived(
	not_null<Main::Session*> session,
	const MTPupdates_ChannelDifference &difference);
// The "Plugins" submenu of a message context menu.
void FillMessageMenu(not_null<Ui::PopupMenu*> menu, HistoryItem *item);
// Application::eventFilter, KeyPress.
[[nodiscard]] bool HandleHotkey(
	not_null<QObject*> object,
	not_null<QKeyEvent*> event);

} // namespace Luxury::Plugins
