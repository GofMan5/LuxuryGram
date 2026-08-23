// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "luxury/utils/telegram_helpers.h"

#include "apiwrap.h"
#include "lang_auto.h"
#include "api/api_common.h"
#include "luxury/luxury_settings.h"
#include "luxury/luxury_state.h"
#include "luxury/luxury_worker.h"
#include "luxury/data/messages_storage.h"
#include "luxury/features/filters/filters_controller.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "core/mime_type.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_document.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_histories.h"
#include "data/data_peer_id.h"
#include "data/data_photo.h"
#include "data/data_poll.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_unread_things.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "styles/style_luxury_styles.h"
#include "styles/style_info.h"
#include "ui/emoji_config.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_entity.h"
#include "window/window_controller.h"

#include <atomic>
#include <functional>
#include <latch>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace {

constexpr auto regDateBotId = 6247153446L;
const auto regDateBotUsername = QString("ayugrambot");

const auto kZalgoPattern = QStringLiteral(
	"\\p{Mn}{3,}|[\\x{202A}-\\x{202E}\\x{2066}-\\x{2069}\\x{200E}\\x{200F}\\x{061C}]");

}

Main::Session *getSession(ID userId) {
	if (!userId) {
		return nullptr;
	}
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto session = account->maybeSession()) {
			if (session->uniqueId() == static_cast<uint64>(userId)
				|| session->userId().bare == static_cast<uint64>(userId)) {
				return session;
			}
		}
	}

	return nullptr;
}

void dispatchToMainThread(const std::function<void()> &callback, int delay) {
	if (delay > 0) {
		base::call_delayed(crl::time(delay), qApp, callback);
	} else {
		crl::on_main(callback);
	}
}

ID getDialogIdFromPeer(not_null<PeerData*> peer) {
	ID peerId = peer->id.value & PeerId::kChatTypeMask;
	if (peer->isChannel() || peer->isChat()) {
		peerId = -peerId;
	}

	return peerId;
}

ID getBareDialogId(ID dialogId) {
	const auto value = static_cast<uint64>(dialogId);
	const auto magnitude = (dialogId < 0) ? (uint64(0) - value) : value;
	return static_cast<ID>(magnitude & PeerId::kChatTypeMask);
}

ID getBareID(not_null<PeerData*> peer) {
	return peer->id.value & PeerId::kChatTypeMask;
}

bool isMessageHidden(const not_null<HistoryItem*> item) {
	if (LuxuryState::isHidden(item)) {
		return true;
	}

	return FiltersController::filtered(item);
}

void MarkAsReadChatList(not_null<Dialogs::MainList*> list) {
	auto mark = std::vector<not_null<History*>>();
	for (const auto &row : list->indexed()->all()) {
		if (const auto history = row->history()) {
			mark.push_back(history);
		}
	}
	ranges::for_each(mark, MarkAsReadThread);
}

void readMentions(base::weak_ptr<Data::Thread> weakThread) {
	const auto thread = weakThread.get();
	if (!thread) {
		return;
	}
	const auto peer = thread->peer();
	const auto topic = thread->asTopic();
	const auto rootId = topic ? topic->rootId() : 0;
	using Flag = MTPmessages_ReadMentions::Flag;
	peer->session().api().request(MTPmessages_ReadMentions(
		MTP_flags(rootId ? Flag::f_top_msg_id : Flag()),
		peer->input(),
		MTP_int(rootId)
	)).done([=](const MTPmessages_AffectedHistory &result)
	{
		const auto offset = peer->session().api().applyAffectedHistory(
			peer,
			result);
		if (offset > 0) {
			readMentions(weakThread);
		} else {
			peer->owner().history(peer)->clearUnreadMentionsFor(rootId);
		}
	}).send();
}

void readReactions(base::weak_ptr<Data::Thread> weakThread) {
	const auto thread = weakThread.get();
	if (!thread) {
		return;
	}
	const auto topic = thread->asTopic();
	const auto sublist = thread->asSublist();
	const auto peer = thread->peer();
	const auto rootId = topic ? topic->rootId() : 0;
	using Flag = MTPmessages_ReadReactions::Flag;
	peer->session().api().request(MTPmessages_ReadReactions(
		MTP_flags(rootId ? Flag::f_top_msg_id : Flag(0)),
		peer->input(),
		MTP_int(rootId),
		sublist ? sublist->sublistPeer()->input() : MTPInputPeer()
	)).done([=](const MTPmessages_AffectedHistory &result)
	{
		const auto offset = peer->session().api().applyAffectedHistory(
			peer,
			result);
		if (offset > 0) {
			readReactions(weakThread);
		} else {
			peer->owner().history(peer)->clearUnreadReactionsFor(rootId, sublist);
		}
	}).send();
}

void MarkAsReadThread(not_null<Data::Thread*> thread) {
	const auto readHistoryNative = [&](const not_null<History*> history)
	{
		history->owner().histories().readInbox(history);
	};
	const auto sendReadMentions = [=](
		const not_null<Data::Thread*> threadInner)
	{
		readMentions(base::make_weak(threadInner));
	};
	const auto sendReadReactions = [=](
		const not_null<Data::Thread*> threadInner)
	{
		readReactions(base::make_weak(threadInner));
	};

	if (thread->chatListBadgesState().unread) {
		if (const auto forum = thread->asForum()) {
			forum->enumerateTopics([](
				not_null<Data::ForumTopic*> topic)
				{
					MarkAsReadThread(topic);
				});
		} else if (const auto topic = thread->asTopic()) {
			topic->readTillEnd();
		} else if (const auto history = thread->asHistory()) {
			readHistoryNative(history);
			if (const auto migrated = history->migrateSibling()) {
				readHistoryNative(migrated);
			}
		}
	}

	if (thread->unreadMentions().has()) {
		sendReadMentions(thread);
	}

	if (thread->unreadReactions().has()) {
		sendReadReactions(thread);
	}

	LuxuryWorker::markAsOnline(&thread->session());
}

void readHistory(not_null<HistoryItem*> message) {
	const auto history = message->history();
	const auto tillId = message->id;

	history->session().data().histories()
		.sendRequest(history,
					 Data::Histories::RequestType::ReadInbox,
					 [=](Fn<void()> finish)
					 {
						 if (const auto channel = history->peer->asChannel()) {
							 return history->session().api().request(MTPchannels_ReadHistory(
								 channel->inputChannel(),
								 MTP_int(tillId)
							 )).done([=] { LuxuryWorker::markAsOnline(&history->session()); }).send();
						 }

						 return history->session().api().request(MTPmessages_ReadHistory(
							 history->peer->input(),
							 MTP_int(tillId)
						 )).done([=](const MTPmessages_AffectedMessages &result)
						 {
							 history->session().api().applyAffectedMessages(history->peer, result);
							 LuxuryWorker::markAsOnline(&history->session());
						 }).fail([=]
						 {
						 }).send();
					 });

	if (history->unreadMentions().has()) {
		readMentions(history->asThread());
	}

	if (history->unreadReactions().has()) {
		readReactions(history->asThread());
	}
}

void markReadAfterAction(not_null<History*> history) {
	const auto &ghost = LuxurySettings::ghost(&history->session());
	if (ghost.sendReadMessages() || !ghost.markReadAfterAction()) {
		return;
	}
	if (const auto last = history->lastServerMessage()) {
		readHistory(last);
	}
}

QString formatTTL(int time, bool isDoc) {
	if (time == 0x7FFFFFFF) {
		return isDoc ? tr::luxury_OnePlayTTL(tr::now) : tr::luxury_OneViewTTL(tr::now);
	}

	return QString("%1s").arg(time);
}

QString getDCName(int dc) {
	const auto getName = [=]
	{
		switch (dc) {
			case 1:
			case 3: return "Miami FL, USA";
			case 2:
			case 4: return "Amsterdam, NL";
			case 5: return "Singapore, SG";
			default: return "UNKNOWN";
		}
	};

	if (dc < 1) {
		return {"DC_UNKNOWN"};
	}

	return QString("DC%1, %2").arg(dc).arg(getName());
}

QString getLocalizedAt() {
	static const auto val = tr::lng_mediaview_date_time(
		tr::now,
		lt_date,
		"",
		lt_time,
		"");
	return val;
}

QString formatDateTime(const QDateTime &date) {
	const auto locale = QLocale::system();
	const auto datePart = locale.toString(date.date(), QLocale::ShortFormat);
	const auto timePart = locale.toString(date, "HH:mm:ss");

	return datePart + getLocalizedAt() + timePart;
}

QString formatMessageTime(const QTime &time) {
	// Called for every message on layout, and the format only depends on the
	// system locale, which does not change while we are running. Building a
	// QLocale and asking it for the format each time was pure overhead.
	static const auto locale = QLocale();
	static const auto shortFormat = locale.timeFormat(QLocale::ShortFormat);
	static const auto secondsFormat = [] {
		// Insert the seconds right after the minutes, reusing the separator the
		// locale already picked, so "h:mm AP" and "H.mm" both stay themselves.
		const auto minutes = shortFormat.indexOf(u"mm"_q);
		if (minutes < 1) {
			return shortFormat.contains(u"AP"_q, Qt::CaseInsensitive)
				? u"h:mm:ss AP"_q
				: u"HH:mm:ss"_q;
		}
		auto result = shortFormat;
		return result.insert(minutes + 2, u"%1ss"_q.arg(
			shortFormat.at(minutes - 1)));
	}();

	return locale.toString(
		time,
		LuxurySettings::getInstance().showMessageSeconds()
			? secondsFormat
			: shortFormat);
}

int getMediaSizeBytes(not_null<HistoryItem*> message) {
	if (!message->media()) {
		return -1;
	}

	const auto media = message->media();

	const auto document = media->document();
	const auto photo = media->photo();

	int64 size = -1;
	if (document) {
		// any file
		size = document->size;
	} else if (photo && photo->hasVideo()) {
		// video
		size = photo->videoByteSize(Data::PhotoSize::Large);
		if (size == 0) {
			size = photo->videoByteSize(Data::PhotoSize::Small);
		}
		if (size == 0) {
			size = photo->videoByteSize(Data::PhotoSize::Thumbnail);
		}
	} else if (photo && !photo->hasVideo()) {
		// photo
		size = photo->imageByteSize(Data::PhotoSize::Large);
		if (size == 0) {
			size = photo->imageByteSize(Data::PhotoSize::Small);
		}
		if (size == 0) {
			size = photo->imageByteSize(Data::PhotoSize::Thumbnail);
		}
	}

	return size;
}

QString getMediaSize(not_null<HistoryItem*> message) {
	const auto size = getMediaSizeBytes(message);

	if (size == -1) {
		return {};
	}

	return Ui::FormatSizeText(size);
}

QString getMediaMime(not_null<HistoryItem*> message) {
	if (!message->media()) {
		return {};
	}

	const auto media = message->media();

	const auto document = media->document();
	const auto photo = media->photo();

	if (document) {
		// any file
		return document->mimeString();
	} else if (photo && photo->hasVideo()) {
		// video
		return "video/mp4";
	} else if (photo && !photo->hasVideo()) {
		// photo
		return "image/jpeg";
	}

	return {};
}

QString getMediaName(not_null<HistoryItem*> message) {
	if (!message->media()) {
		return {};
	}

	const auto media = message->media();

	if (const auto document = media->document()) {
		return document->filename();
	}

	return {};
}

QString getMediaResolution(not_null<HistoryItem*> message) {
	if (!message->media()) {
		return {};
	}

	const auto media = message->media();

	const auto document = media->document();
	const auto photo = media->photo();

	const auto formatQSize = [=](QSize size)
	{
		if (size.isNull() || size.isEmpty() || !size.isValid()) {
			return QString();
		}

		return QString("%1x%2").arg(size.width()).arg(size.height());
	};

	if (document) {
		return formatQSize(document->dimensions);
	} else if (photo) {
		auto result = photo->size(Data::PhotoSize::Large);
		if (!result.has_value()) {
			result = photo->size(Data::PhotoSize::Small);
		}
		if (!result.has_value()) {
			result = photo->size(Data::PhotoSize::Thumbnail);
		}
		return result ? formatQSize(*result) : QString();
	}

	return {};
}

QString getMediaDC(not_null<HistoryItem*> message) {
	if (!message->media()) {
		return {};
	}

	const auto media = message->media();

	const auto document = media->document();
	const auto photo = media->photo();

	if (document) {
		return getDCName(document->getDC());
	} else if (photo) {
		return getDCName(photo->getDC());
	}

	return {};
}

QString getPeerDC(not_null<PeerData*> peer) {
	if (const auto statsDcId = peer->owner().statsDcId(peer)) {
		return getDCName(statsDcId);
	}

	if (peer->hasUserpic()) {
		const auto dc = v::match(
			peer->userpicLocation().file().data,
			[&](const StorageFileLocation &data)
			{
				return data.dcId();
			},
			[&](const WebFileLocation &)
			{
				// should't happen, but still
				// all webpages are on DC4
				return 4;
			},
			[&](const GeoPointLocation &)
			{
				// shouldn't happen naturally
				return 0;
			},
			[&](const AudioAlbumThumbLocation &)
			{
				// shouldn't happen naturally
				return 0;
			},
			[&](const PlainUrlLocation &)
			{
				// should't happen, but still
				// all webpages are on DC4
				return 4;
			},
			[&](const InMemoryLocation &)
			{
				// shouldn't happen naturally
				return 0;
			});

		if (dc > 0) {
			return getDCName(dc);
		}
	}

	return {};
}

int getScheduleTime(int64 sumSize) {
	auto time = 12;
	time += (int) std::ceil(std::max(6.0, std::ceil(sumSize / 1024.0 / 1024.0 * 0.7))) + 1;
	return time;
}

bool isMessageSavable(const not_null<HistoryItem*> item) {
	const auto &settings = LuxurySettings::getInstance();

	if (!settings.saveDeletedMessages()) {
		return false;
	}

	if (const auto possiblyBot = item->history()->peer->asUser()) {
		return !possiblyBot->isBot() || (settings.saveForBots() && possiblyBot->isBot());
	}
	return true;
}

void processMessageDelete(not_null<HistoryItem*> item) {
	if (!isMessageSavable(item)) {
		item->destroy();
	} else if (!item->isDeleted()) {
		if (item->ttlDestroyAt() > 0) {
			item->applyTTL(0);
		}
		item->setDeleted();
		LuxuryMessages::addDeletedMessage(item);
	}
}

void processMessagesDelete(
		const std::vector<not_null<HistoryItem*>> &items) {
	auto toStore = std::vector<not_null<HistoryItem*>>();
	toStore.reserve(items.size());
	for (const auto &item : items) {
		Expects(isMessageSavable(item));
		if (item->isDeleted()) {
			continue;
		}
		if (item->ttlDestroyAt() > 0) {
			item->applyTTL(0);
		}
		item->setDeleted();
		toStore.push_back(item);
	}
	LuxuryMessages::addDeletedMessages(toStore);
}

void resolvePeer(
	const QString &peerId,
	const QString &username,
	Main::Session *session,
	const UsernameResolverCallback &callback) {
	auto normalized = username.trimmed().toLower();
	if (normalized.isEmpty()) {
		callback(QString(), nullptr);
		return;
	}
	normalized = normalized.startsWith("@") ? normalized.mid(1) : normalized;

	if (normalized.isEmpty()) {
		callback(QString(), nullptr);
		return;
	}

	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(normalized),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result)
	{
		Expects(result.type() == mtpc_contacts_resolvedPeer);

		auto &data = result.c_contacts_resolvedPeer();
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		if (const auto peer = session->data().peerLoaded(peerFromMTP(data.vpeer()))) {
			if (QString::number(peer->id.value & PeerId::kChatTypeMask) == peerId) {
				callback(normalized, peer);
				return;
			}
		}

		callback(normalized, nullptr);
	}).fail([=]
	{
		callback(QString(), nullptr);
	}).send();
}

void searchPeer(
		const QString &,
		Main::Session *,
		const UsernameResolverCallback &callback) {
	callback(QString(), nullptr);
}

void searchUserById(ID userId, Main::Session *session, const UsernameResolverCallback &callback) {
	if (userId == 0 || !session) {
		callback(QString(), nullptr);
		return;
	}

	if (const auto userLoaded = session->data().userLoaded(userId)) {
		callback(userLoaded->username(), userLoaded);
		return;
	}

	searchPeer(
		QString::number(userId),
		session,
		[=](const QString &title, PeerData *data)
		{
			const auto user = data ? data->asUser() : nullptr;
			if (user && user->accessHash()) {
				callback(title, user);
				return;
			}
			callback(QString(), nullptr);
		});
}

void searchChatById(ID chatId, Main::Session *session, const UsernameResolverCallback &callback) {
	if (chatId == 0 || !session) {
		callback(QString(), nullptr);
		return;
	}

	if (const auto channelLoaded = session->data().channelLoaded(chatId)) {
		callback(channelLoaded->username(), channelLoaded);
		return;
	}

	if (const auto chatLoaded = session->data().chatLoaded(chatId)) {
		callback(chatLoaded->username(), chatLoaded);
		return;
	}

	searchPeer(
		QString("-100") + QString::number(chatId),
		session,
		[=](const QString &title, PeerData *data)
		{
			if (data && (data->isChat() || data->isChannel())) {
				callback(title, data);
			} else {
				callback(QString(), nullptr);
			}
		});
}

ID getUserIdFromPackId(uint64 id) {
	// https://github.com/TDesktop-x64/tdesktop/pull/218/commits/844e5f0ab116e7639cfc79633a68afe8fdcbc463
	auto ownerId = id >> 32;
	if ((id >> 16 & 0xff) == 0x3f) {
		ownerId |= 0x80000000;
	}
	if (id >> 24 & 0xff) {
		ownerId += 0x100000000;
	}

	return ownerId;
}

bool mediaDownloadable(const Data::Media *media) {
	return media
		&& !media->webpage()
		&& (media->photo() || media->document());
}

TextWithTags extractText(not_null<HistoryItem*> item) {
	auto text = item->originalText();
	if (const auto media = item->media()) {
		if (const auto poll = media->poll()) {
			text = TextWithEntities();
			text.append(u"📊 "_q).append(poll->question).append(u'\n');
			for (const auto &answer : poll->answers) {
				text.append(u"• "_q).append(answer.text).append(u'\n');
			}
		} else if (text.text.isEmpty() && !mediaDownloadable(media)) {
			text = media->clipboardText().rich;
		}
	}

	return {
		.text = std::move(text.text),
		.tags = TextUtilities::ConvertEntitiesToTextTags(text.entities),
	};
}

static bool prependPseudoReplyImpl(
		not_null<Main::Session*> session,
		not_null<History*> history,
		TextWithTags &textWithTags,
		FullReplyTo &replyTo) {
	if (!replyTo) {
		return false;
	}
	const auto replyItem = session->data().message(replyTo.messageId);
	if (!replyItem || !replyItem->isDeleted()) {
		return false;
	}
	const auto shortify = [&](const QString &text, int maxLength) {
		if (text.isEmpty() || text.length() < maxLength) {
			return text;
		}
		return text.left(maxLength - 1) + QChar(8230); // …
	};
	const auto shiftEntities = [&](QVector<TextWithTags::Tag> &tags, int offset) {
		if (tags.isEmpty() || !offset) {
			return;
		}
		for (auto &tag : tags) {
			tag.offset += offset;
		}
	};

	const auto from = replyItem->from();
	auto name = QString();
	if (!history->peer->isUser() || replyItem->history()->peer != history->peer) {
		name = from->name();
	}

	auto msgText = !replyTo.quote.empty()
		? replyTo.quote.text
		: replyItem->originalText().text;
	if (msgText.isEmpty()) {
		msgText = replyItem->notificationText().text;
	}
	const auto shortifiedText = shortify(msgText, 100);

	const auto prefix = name.isEmpty()
		? shortifiedText
		: (name + "\n" + shortifiedText);

	if (textWithTags.empty()) {
		textWithTags.text = prefix;
	} else {
		textWithTags.text.prepend(prefix + "\n");
	}
	const auto prefixLength = prefix.length() + (textWithTags.text.length() > prefix.length() ? 1 : 0);

	shiftEntities(textWithTags.tags, prefixLength);

	EntitiesInText newEntities;
	const auto nameLength = int(name.length());

	newEntities.push_back(EntityInText{
		EntityType::Blockquote,
		0,
		int(prefix.length()),
		{}
	});

	if (nameLength > 0) {
		newEntities.push_back(EntityInText{
			EntityType::Bold,
			0,
			nameLength,
			QString()
		});

		if (const auto user = from->asUser()) {
			if (const auto accessHash = user->accessHash()) {
				const auto mentionData = QStringLiteral("%1.%2:%3")
					.arg(user->id.value)
					.arg(accessHash)
					.arg(session->userId().bare);

				newEntities.push_back(EntityInText{
					EntityType::MentionName,
					0,
					nameLength,
					mentionData
				});
			}
		}
	}

	const auto newTags = TextUtilities::ConvertEntitiesToTextTags(newEntities);
	textWithTags.tags.append(newTags);

	return true;
}

bool prependPseudoReply(Api::MessageToSend &message) {
	if (!message.action.history) {
		return false;
	}
	return prependPseudoReplyImpl(
		&message.action.history->session(),
		message.action.history,
		message.textWithTags,
		message.action.replyTo);
}

bool prependPseudoReply(
		not_null<Main::Session*> session,
		not_null<History*> history,
		TextWithTags &caption,
		FullReplyTo &replyTo) {
	return prependPseudoReplyImpl(session, history, caption, replyTo);
}

TextWithEntities reverseLocalPremiumEmoji(const TextWithEntities &text, not_null<History *> history, bool isForQuote) {
	if (text.empty()) {
		return text;
	}

	const auto channel = history->peer->asChannel();
	const auto hasCustomEmoji = channel && channel->mgInfo && channel->mgInfo->emojiSet.id;
	const auto sets = hasCustomEmoji && channel
		? &channel->owner().stickers().sets()
		: nullptr;
	const auto set = sets
		? sets->find(channel->mgInfo->emojiSet.id)
		: decltype(sets->cend()){};
	const auto premium = (history->owner().session().user()->flags()
		& UserDataFlag::Premium);
	const auto emojiAllowed = [=](const EntityInText& entity)
	{
		if (!sets || set == sets->cend()) {
			return false;
		}
		const auto emojiId = Data::ParseCustomEmojiData(entity.data());
		if (!emojiId) {
			return false;
		}
		const auto &emojiMap = set->second->emoji;
		for (const auto &[emoji, documents] : emojiMap) {
			for (const auto &document : documents) {
				if (document->id == emojiId) {
					return true;
				}
			}
		}
		return false;
	};

	auto result = text;
	for (auto &entity : result.entities) {
		if (entity.type() != EntityType::CustomEmoji) {
			continue;
		}
		const auto shouldConvert = entity.isLocal()
			? (isForQuote
				|| (!history->peer->isSelf() && !premium && !emojiAllowed(entity)))
			: (!isForQuote
				&& !history->peer->isSelf()
				&& !premium
				&& !emojiAllowed(entity));
		if (shouldConvert) {
			entity = EntityInText(
				EntityType::CustomUrl,
				entity.offset(),
				entity.length(),
				u"tg://emoji?id="_q + entity.data());
		}
	}
	return result;
}

void applyLocalPremiumEmoji(TextWithEntities &text) {
	static const auto kLocalPremiumEmojiRegex = QRegularExpression(
		QStringLiteral("^tg://emoji\\?id=(\\d+)$"));

	for (auto &entity : text.entities) {
		if (entity.type() == EntityType::CustomUrl) {
			const auto match = kLocalPremiumEmojiRegex.match(entity.data());
			if (match.hasMatch()) {
				const auto entityText = text.text.mid(
					entity.offset(),
					entity.length());
				auto emojiLength = 0;
				const auto emoji = Ui::Emoji::Find(entityText, &emojiLength);
				if (emoji && emojiLength == entityText.size()) {
					const auto emojiId = match.captured(1);
					auto ok = false;
					emojiId.toULongLong(&ok);
					if (ok) {
						entity = EntityInText(
							EntityType::CustomEmoji,
							entity.offset(),
							entity.length(),
							emojiId);
						entity.setLocal();
					}
				}
			}
		}
	}
}

not_null<Main::Session*> currentSession() {
	return &Core::App().domain().active().session();
}

template<typename T>
PeerData *getPeerFromDialogId(T id) {
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto session = account->maybeSession()) {
			PeerData *from = session->data().userLoaded(id);
			if (!from) {
				from = session->data().channelLoaded(id);
			}
			if (!from) {
				from = session->data().chatLoaded(id);
			}

			if (from) {
				return from;
			}
		}
	}

	return nullptr;
}

PeerData *getPeerFromDialogId(ID id) {
	return getPeerFromDialogId<ID>(id);
}

PeerData *getPeerFromDialogId(unsigned long long id) {
	return getPeerFromDialogId<unsigned long long>(id);
}

QString filterZalgo(const QString &text) {
	static const auto regex = QRegularExpression(
		kZalgoPattern,
		QRegularExpression::UseUnicodePropertiesOption);

	auto match = regex.match(text);
	if (!match.hasMatch()) {
		return text;
	}

	QString output;
	output.reserve(text.length());
	int lastEnd = 0;

	auto it = regex.globalMatch(text);
	while (it.hasNext()) {
		match = it.next();
		output.append(text.mid(lastEnd, match.capturedStart() - lastEnd));
		const int matchLength = match.capturedLength();
		for (int i = 0; i < matchLength; i++) {
			output.append(QChar(0x2060));
		}
		lastEnd = match.capturedEnd();
	}
	output.append(text.mid(lastEnd));

	return output;
}

void getUserRegistrationDateInner(
	not_null<UserData*> user,
	ID botId,
	Fn<void(TextWithEntities)> callback) {
	const auto session = &user->session();
	const auto userId = getBareID(user);
	const auto userName = user->name();
	const auto isSelf = user->isSelf();

	const auto bot = session->data().userLoaded(botId);
	if (!bot) {
		callback(TextWithEntities{});
		return;
	}

	session->api().request(MTPmessages_GetInlineBotResults(
		MTP_flags(0),
		bot->inputUser(),
		MTP_inputPeerEmpty(),
		MTPInputGeoPoint(),
		MTP_string(qsl("regdate ") + QString::number(userId)),
		MTP_string("")
	)).done([=](const MTPmessages_BotResults &result)
	{
		TextWithEntities resultText;

		if (result.type() != mtpc_messages_botResults) {
			callback(resultText);
			return;
		}

		auto &d = result.c_messages_botResults();
		session->data().processUsers(d.vusers());

		auto &v = d.vresults().v;

		for (const auto &res : v) {
			const auto message = res.match(
				[&](const MTPDbotInlineResult &data)
				{
					return &data.vsend_message();
				},
				[&](const MTPDbotInlineMediaResult &data)
				{
					return &data.vsend_message();
				});

			const auto text = message->match(
				[&](const MTPDbotInlineMessageMediaAuto &data)
				{
					return QString();
				},
				[&](const MTPDbotInlineMessageText &data)
				{
					return qs(data.vmessage());
				},
				[&](const MTPDbotInlineMessageMediaGeo &data)
				{
					return QString();
				},
				[&](const MTPDbotInlineMessageMediaVenue &data)
				{
					return QString();
				},
				[&](const MTPDbotInlineMessageMediaContact &data)
				{
					return QString();
				},
				[&](const MTPDbotInlineMessageMediaInvoice &data)
				{
					return QString();
				},
				[&](const MTPDbotInlineMessageMediaWebPage &data)
				{
					return QString();
				},
				[&](const MTPDbotInlineMessageRichMessage &data)
				{
					return QString();
				});

			if (text.isEmpty() || text == "failed") {
				continue;
			}

			const auto json = QJsonDocument::fromJson(text.toUtf8());
			if (!json.isObject()) {
				continue;
			}

			const auto obj = json.object();
			const auto flag = obj["flag"].toString();
			const auto date = obj["date"].toString();

			const auto parsedDate = QDate::fromString(date, "dd.MM.yyyy");
			const auto formattedDate = langDayOfMonthFull(parsedDate);

			if (flag == "EXACT" || flag == "INTERPOLATED") {
				if (!isSelf) {
					resultText = tr::luxury_CreationDateUserApproximately(
						tr::now,
						lt_item1,
						TextWithEntities{userName},
						lt_item2,
						TextWithEntities{formattedDate},
						tr::rich
					);
				} else {
					resultText = tr::luxury_CreationDateSelfApproximately(
						tr::now,
						lt_item,
						TextWithEntities{formattedDate},
						tr::rich
					);
				}
			} else if (flag == "LT") {
				if (!isSelf) {
					resultText = tr::luxury_CreationDateUserEarlier(
						tr::now,
						lt_item1,
						TextWithEntities{userName},
						lt_item2,
						TextWithEntities{formattedDate},
						tr::rich
					);
				} else {
					resultText = tr::luxury_CreationDateSelfEarlier(
						tr::now,
						lt_item,
						TextWithEntities{formattedDate},
						tr::rich
					);
				}
			} else if (flag == "ET") {
				if (!isSelf) {
					resultText = tr::luxury_CreationDateUserLater(
						tr::now,
						lt_item1,
						TextWithEntities{userName},
						lt_item2,
						TextWithEntities{formattedDate},
						tr::rich
					);
				} else {
					resultText = tr::luxury_CreationDateSelfLater(
						tr::now,
						lt_item,
						TextWithEntities{formattedDate},
						tr::rich
					);
				}
			}
			break;
		}

		callback(resultText);
	}).fail([=]
	{
		callback(TextWithEntities{});
	}).handleAllErrors().send();
}

void getUserRegistrationDate(not_null<UserData*> user, Fn<void(TextWithEntities)> callback) {
	const auto session = &user->session();
	const auto botId = regDateBotId;
	const auto botUsername = regDateBotUsername;

	if (session->data().userLoaded(botId)) {
		getUserRegistrationDateInner(user, botId, callback);
	} else {
		resolvePeer(
			QString::number(botId),
			botUsername,
			session,
			[=](const QString &title, PeerData *data)
			{
				getUserRegistrationDateInner(user, botId, callback);
			});
	}
}

void getChannelJoinOrCreateDate(not_null<ChannelData*> channel, Fn<void(TextWithEntities)> callback) {
	TextWithEntities result;

	if (channel->inviteDate) {
		const auto formattedDate = langDayOfMonthFull(base::unixtime::parse(channel->inviteDate).date());
		result = tr::luxury_JoinDateChat(
			tr::now,
			lt_item1,
			TextWithEntities{channel->name()},
			lt_item2,
			TextWithEntities{formattedDate},
			tr::rich
		);
	} else if (channel->date) {
		const auto formattedDate = langDayOfMonthFull(base::unixtime::parse(channel->date).date());
		result = tr::luxury_CreationDateChat(
			tr::now,
			lt_item1,
			TextWithEntities{channel->name()},
			lt_item2,
			TextWithEntities{formattedDate},
			tr::rich
		);
	}

	if (callback) {
		callback(result);
	}
}

void getChatCreateDate(not_null<ChatData*> chat, Fn<void(TextWithEntities)> callback) {
	TextWithEntities result;

	if (chat->date) {
		const auto formattedDate = langDayOfMonthFull(base::unixtime::parse(chat->date).date());
		result = tr::luxury_CreationDateChat(
			tr::now,
			lt_item1,
			TextWithEntities{chat->name()},
			lt_item2,
			TextWithEntities{formattedDate},
			tr::rich
		);
	}

	if (callback) {
		callback(result);
	}
}

void getRegistrationDate(not_null<PeerData*> peer, Fn<void(TextWithEntities)> callback) {
	if (const auto user = peer->asUser()) {
		getUserRegistrationDate(user, callback);
	} else if (const auto channel = peer->asChannel()) {
		getChannelJoinOrCreateDate(channel, callback);
	} else if (const auto chat = peer->asChat()) {
		getChatCreateDate(chat, callback);
	} else {
		if (callback) {
			callback(TextWithEntities{});
		}
	}
}

QString getBetterLinkPreview(const QString &url) {
	const auto &settings = LuxurySettings::getInstance();
	if (!settings.improveLinkPreviews()) {
		return url;
	}

	auto parsed = QUrl(url);
	if (!parsed.isValid() || parsed.host().isEmpty()) {
		return url;
	}

	auto host = parsed.host().toLower();

	if (host == u"twitter.com"_q || host == u"x.com"_q) {
		parsed.setHost(u"fixupx.com"_q);
	} else if (host == u"tiktok.com"_q || host.endsWith(u".tiktok.com"_q)) {
		host.replace(u"tiktok.com"_q, u"kktiktok.com"_q);
		parsed.setHost(host);
	} else if (host == u"reddit.com"_q || host == u"www.reddit.com"_q) {
		parsed.setHost(u"vxreddit.com"_q);
	} else if (host == u"instagram.com"_q || host == u"www.instagram.com"_q) {
		parsed.setHost(u"kkclip.com"_q);
	} else if (host == u"pixiv.net"_q || host == u"www.pixiv.net"_q) {
		parsed.setHost(u"phixiv.net"_q);
	} else {
		return url;
	}

	return parsed.toString();
}

void applyGhostScheduling(
		not_null<Main::Session*> session,
		Api::SendOptions &options,
		int delaySeconds) {
	const auto &ghost = LuxurySettings::ghost(session);
	if (ghost.isUseScheduledMessages() && !options.scheduled) {
		const auto delay = Core::App().settings().proxy().isEnabled()
			? (delaySeconds * 6 + 4) / 5 //ceil(delaySeconds * 1.2)
			: delaySeconds;
		options.scheduled = base::unixtime::now() + delay;
	}
}
