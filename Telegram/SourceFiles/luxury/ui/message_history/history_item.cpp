// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/ui/message_history/history_item.h"

#include "api/api_text_entities.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "luxury/data/entities.h"
#include "luxury/utils/luxury_mapper.h"
#include "ui/text/text_utilities.h"

namespace MessageHistory {

OwnedItem::OwnedItem(std::nullptr_t) {
}

OwnedItem::OwnedItem(
	not_null<HistoryView::ElementDelegate*> delegate,
	not_null<HistoryItem*> data)
	: _data(data), _view(_data->createView(delegate)) {
}

OwnedItem::OwnedItem(OwnedItem &&other)
	: _data(base::take(other._data)), _view(base::take(other._view)) {
}

OwnedItem &OwnedItem::operator=(OwnedItem &&other) {
	if (this != &other) {
		// Same order as the admin log's OwnedItem: the outgoing view goes
		// with the _view assignment, before the item it points to.
		const auto old = base::take(_data);
		_data = base::take(other._data);
		_view = base::take(other._view);
		if (old) {
			old->destroy();
		}
	}
	return *this;
}

OwnedItem::~OwnedItem() {
	clearView();
	if (_data) {
		_data->destroy();
	}
}

void OwnedItem::refreshView(
	not_null<HistoryView::ElementDelegate*> delegate) {
	_view = _data->createView(delegate);
}

void OwnedItem::clearView() {
	_view = nullptr;
}

void GenerateItems(
	not_null<HistoryView::ElementDelegate*> delegate,
	not_null<History*> history,
	const LuxuryMessageBase &message,
	Fn<void(OwnedItem item, TimeId sentDate, MsgId)> callback) {
	const auto fromId = PeerId(PeerIdHelper(static_cast<BareId>(message.fromId)));
	PeerData *from = nullptr;
	if (fromId.is<UserId>()) {
		from = history->owner().userLoaded(fromId.to<UserId>());
	} else if (fromId.is<ChannelId>()) {
		from = history->owner().channelLoaded(fromId.to<ChannelId>());
	} else if (fromId.is<ChatId>()) {
		from = history->owner().chatLoaded(fromId.to<ChatId>());
	}
	if (!from && fromId.is<UserId>()) {
		const auto bare = fromId.value & PeerId::kChatTypeMask;
		from = history->owner().channelLoaded(ChannelId(bare));
		if (!from) {
			from = history->owner().chatLoaded(ChatId(bare));
		}
	}
	const auto date = message.entityCreateDate;
	const auto addPart = [&](
		not_null<HistoryItem*> item,
		TimeId sentDate = 0,
		MsgId realId = MsgId())
	{
		return callback(OwnedItem(delegate, item), sentDate, realId);
	};

	const auto makeSimpleTextMessage = [&](TextWithEntities &&text)
	{
		base::flags<MessageFlag> flags = MessageFlag::AdminLogEntry;
		if (from) {
			flags |= MessageFlag::HasFromId;
		} else {
			flags |= MessageFlag::HasPostAuthor;
		}
		if (!message.postAuthor.empty()) {
			flags |= MessageFlag::HasPostAuthor;
		}

		return history->makeMessage({
										.id = history->nextNonHistoryEntryId(),
										.flags = flags,
										.from = from ? from->id : 0,
										.date = date,
										.postAuthor = !message.postAuthor.empty()
														  ? QString::fromStdString(message.postAuthor)
														  : from
																? QString()
																: tr::luxury_UnknownPeer(tr::now, lt_id, QString::number(message.fromId)),
									},
									std::move(text),
									MTP_messageMediaEmpty());
	};

	const auto addSimpleTextMessage = [&](TextWithEntities &&text)
	{
		// The bubble shows when the copy was saved; the tooltip adds when
		// the message itself was sent.
		addPart(
			makeSimpleTextMessage(std::move(text)),
			TimeId(message.date),
			MsgId(message.messageId));
	};

	const auto text = QString::fromStdString(message.text);
	auto textAndEntities = Ui::Text::WithEntities(text);
	const auto entities = LuxuryMapper::deserializeTextWithEntities(message.textEntities);
	textAndEntities.entities = Api::EntitiesFromMTP(&history->session(), entities.v);
	addSimpleTextMessage(std::move(textAndEntities));
}

} // namespace MessageHistory
