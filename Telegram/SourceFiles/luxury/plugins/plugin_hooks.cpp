// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#include "luxury/plugins/plugin_hooks.h"

#include "luxury/plugins/plugin_host_api.h"
#include "luxury/plugins/plugin_host_logic.h"
#include "luxury/plugins/plugin_manager.h"
#include "luxury/plugins/plugin_payloads.h"
#include "luxury/plugins/core/tl_codec.h"
#include "luxury/utils/telegram_helpers.h"
#include "api/api_common.h"
#include "core/application.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/text/text_entity.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
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
static_assert(int(std::size(kEntityNames))
	== int(EntityType::FormattedDate) + 1);
static_assert(kEntityTextUrl == int(EntityType::CustomUrl));
static_assert(kEntityMentionName == int(EntityType::MentionName));
static_assert(kEntityCustomEmoji == int(EntityType::CustomEmoji));
static_assert(kEntityPre == int(EntityType::Pre));

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

// A mentionName carries "userId.accessHash:selfId": a plugin sees the
// user id only, and a mention of 777000 not at all (EntityJson).
[[nodiscard]] json EntitiesJson(const EntitiesInText &entities) {
	auto result = json::array();
	for (const auto &entity : entities) {
		auto data = entity.data();
		if (entity.type() == EntityType::MentionName) {
			const auto userId = TextUtilities::MentionNameDataToFields(
				data).userId;
			data = userId ? QString::number(userId) : QString();
		}
		auto item = EntityJson({
			.type = int(entity.type()),
			.offset = entity.offset(),
			.length = entity.length(),
			.data = data.toStdString(),
		});
		if (!item.is_null()) {
			result.push_back(std::move(item));
		}
	}
	return result;
}

// What a plugin returned from beforeSend, for the message field's tags.
// A mention needs the user's access hash: one of an unknown user is
// dropped.
[[nodiscard]] EntitiesInText PluginEntities(
		not_null<Main::Session*> session,
		const json &list,
		int textLength) {
	auto result = EntitiesInText();
	for (const auto &entity : ParseEntities(list, textLength)) {
		auto data = QString::fromStdString(entity.data);
		if (entity.type == kEntityMentionName) {
			const auto userId = data.toULongLong();
			const auto user = session->data().userLoaded(UserId(userId));
			if (!user) {
				continue;
			}
			data = TextUtilities::MentionNameDataFromFields({
				.selfId = session->userId().bare,
				.userId = userId,
				.accessHash = user->accessHash(),
			});
		}
		result.push_back(EntityInText(
			EntityType(entity.type),
			entity.offset,
			entity.length,
			data));
	}
	return result;
}

// A plugin's replyTo: a message of the same chat, or null for none. The
// topic stays, as the message field would keep it.
[[nodiscard]] FullReplyTo PluginReply(
		not_null<PeerData*> peer,
		const json &replyTo,
		const FullReplyTo &was) {
	auto result = FullReplyTo();
	result.topicRootId = was.topicRootId;
	result.monoforumPeerId = was.monoforumPeerId;
	if (const auto id = MessageIdArg(replyTo)) {
		result.messageId = FullMsgId(peer->id, MsgId(*id));
	}
	return result;
}

[[nodiscard]] json MediaJson(Data::Media *media) {
	if (!media) {
		return nullptr;
	} else if (const auto photo = media->photo()) {
		return {
			{ "type", "photo" },
			{ "size", photo->imageByteSize(Data::PhotoSize::Large) },
		};
	} else if (const auto document = media->document()) {
		return {
			{ "type", document->sticker()
				? "sticker"
				: document->isVoiceMessage()
				? "voice"
				: document->isVideoMessage()
				? "round"
				: document->isAnimation()
				? "animation"
				: document->isVideoFile()
				? "video"
				: document->isAudioFile()
				? "audio"
				: "file" },
			{ "size", document->size },
		};
	}
	return { { "type", "other" }, { "size", 0 } };
}

[[nodiscard]] json ForwardJson(not_null<HistoryItem*> item) {
	const auto forwarded = item->Get<HistoryMessageForwarded>();
	if (!forwarded) {
		return nullptr;
	}
	const auto sender = forwarded->originalSender;
	const auto hidden = forwarded->originalHiddenSenderInfo.get();
	const auto id = forwarded->originalId;
	return {
		{ "from", (sender
			&& sender->id != PeerData::kServiceNotificationsId)
			? PeerJson(sender)
			: json() },
		{ "name", (!sender && hidden)
			? json(hidden->name.toStdString())
			: json() },
		{ "date", forwarded->originalDate },
		{ "id", id ? json(std::to_string(id.bare)) : json() },
	};
}

// The chat open in the active window, null for none or the service chat.
[[nodiscard]] json ActiveChatJson() {
	const auto window = Core::App().activeWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	const auto peer = controller
		? controller->activeChatCurrent().peer()
		: nullptr;
	return (peer
		&& peer->id != PeerData::kServiceNotificationsId
		&& IsActive(&peer->session()))
		? PeerJson(peer)
		: json();
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
	const auto peer = item->history()->peer;
	const auto from = item->from();
	const auto &text = item->originalText();
	const auto reply = item->replyToId();
	const auto edited = item->Get<HistoryMessageEdited>();
	const auto group = item->groupId();
	return {
		{ "id", std::to_string(item->id.bare) },
		{ "peer", PeerJson(peer) },
		{ "from", (from->id == PeerData::kServiceNotificationsId)
			? json()
			: PeerJson(from) },
		{ "date", item->date() },
		{ "text", text.text.toStdString() },
		{ "entities", EntitiesJson(text.entities) },
		{ "out", item->out() },
		{ "replyTo", reply ? json(std::to_string(reply.bare)) : json() },
		{ "hasMedia", item->media() != nullptr },
		{ "media", MediaJson(item->media()) },
		{ "forward", ForwardJson(item) },
		{ "topicId", peer->isForum()
			? json(std::to_string(item->topicRootId().bare))
			: json() },
		{ "editDate", edited ? json(edited->date) : json() },
		{ "groupedId", group ? json(std::to_string(group.raw())) : json() },
	};
}

TextWithEntities PluginText(
		not_null<Main::Session*> session,
		const std::string &text,
		const json &entities) {
	auto result = TextWithEntities{ QString::fromStdString(text) };
	result.entities = PluginEntities(
		session,
		entities,
		int(result.text.size()));
	return result;
}

bool BeforeSend(Api::MessageToSend &message) {
	const auto manager = Active();
	if (!manager || message.textWithTags.text.isEmpty()) {
		return true;
	}
	const auto peer = message.action.history->peer;
	if (!IsActive(&peer->session())) {
		return true;
	}
	auto fold = SendFold{
		.text = message.textWithTags.text.toStdString(),
		.entities = EntitiesJson(TextUtilities::ConvertTextTagsToEntities(
			message.textWithTags.tags)),
	};
	const auto reply = message.action.replyTo.messageId;
	if (reply && reply.peer == peer->id) {
		fold.replyTo = std::to_string(reply.msg.bare);
	}
	const auto replyWas = fold.replyTo;
	for (const auto &instance : manager->instances()) {
		if (instance->plugin().granted.has(Permission::MessagesModify)) {
			FoldBeforeSend(fold, instance->beforeSend({
				{ "peer", PeerJson(peer) },
				{ "text", fold.text },
				{ "entities", fold.entities },
				{ "replyTo", fold.replyTo },
			}));
		}
	}
	if (fold.cancelled) {
		return false;
	} else if (fold.entitiesChanged) {
		// The old tags belong to the old text: only the plugin's stay.
		message.textWithTags = TextWithTags{
			QString::fromStdString(fold.text),
		};
		message.textWithTags.tags = TextUtilities::ConvertEntitiesToTextTags(
			PluginEntities(
				&peer->session(),
				fold.entities,
				int(message.textWithTags.text.size())));
	}
	if (fold.replyChanged && fold.replyTo != replyWas) {
		message.action.replyTo = PluginReply(
			peer,
			fold.replyTo,
			message.action.replyTo);
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

void MessageEdited(not_null<HistoryItem*> item) {
	EmitMessage(item, "messageEdited");
}

void MessagesDeleted(const std::vector<not_null<HistoryItem*>> &items) {
	const auto manager = Active();
	if (!manager) {
		return;
	}
	for (const auto &item : items) {
		EmitMessage(item, "messageDeleted");
	}
}

void ChatRead(not_null<History*> history, int64 tillId) {
	const auto manager = Active();
	const auto peer = history->peer;
	if (!manager
		|| (peer->id == PeerData::kServiceNotificationsId)
		|| !IsActive(&history->session())) {
		return;
	}
	auto payload = std::optional<json>();
	for (const auto &instance : manager->instances()) {
		// subscribe() refuses chatRead without messages.read.
		if (instance->wants("chatRead")) {
			if (!payload) {
				payload = json{
					{ "peer", PeerJson(peer) },
					{ "maxId", std::to_string(tillId) },
					{ "unread", history->unreadCount() },
				};
			}
			instance->emitLater("chatRead", *payload);
		}
	}
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
			instance->callLater(handler, {
				{ "peer", PeerJson(message->history()->peer) },
				{ "message", full
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
					const auto payload = json{ { "peer", ActiveChatJson() } };
					instance->callLater(hotkey.fn, payload, true);
				}
				return true;
			}
		}
	}
	return false;
}

} // namespace Luxury::Plugins
