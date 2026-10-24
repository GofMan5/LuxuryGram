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
#include "config.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "base/call_delayed.h"
#include "base/debug_log.h"
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
#include <limits>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace {

constexpr auto regDateBotId = 6247153446L;
const auto regDateBotUsername = QString("ayugrambot");

const auto kZalgoPattern = QStringLiteral(
	"\\p{Mn}{3,}|[\\x{202A}-\\x{202E}\\x{2066}-\\x{2069}\\x{200E}\\x{200F}\\x{061C}]");

// Myanmar, Tibetan, Hebrew and Indic letters carry up to four or five marks
// of their own script. Zalgo piles up the generic combining marks, repeats
// one mark, or stacks more than any script does.
constexpr auto kMaxScriptMarks = 5;
constexpr auto kMaxGenericMarks = 2;

[[nodiscard]] bool IsGenericCombiningMark(uint code) {
	return (code >= 0x0300 && code <= 0x036F)
		|| (code >= 0x1AB0 && code <= 0x1AFF)
		|| (code >= 0x1DC0 && code <= 0x1DFF)
		|| (code >= 0x20D0 && code <= 0x20FF)
		|| (code >= 0xFE20 && code <= 0xFE2F);
}

[[nodiscard]] bool IsZalgo(QStringView match) {
	const auto codes = match.toUcs4();
	if (codes.size() > kMaxScriptMarks) {
		return true;
	}
	auto generic = 0;
	auto previous = uint();
	for (const auto code : codes) {
		if ((QChar::category(code) != QChar::Mark_NonSpacing)
			|| (code == previous)
			|| (IsGenericCombiningMark(code)
				&& ++generic > kMaxGenericMarks)) {
			return true;
		}
		previous = code;
	}
	return false;
}

}

Main::Session *getSessionByUniqueId(uint64 sessionId) {
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto session = account->maybeSession();
			session && session->uniqueId() == sessionId) {
			return session;
		}
	}
	return nullptr;
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

void MarkAsReadChatList(not_null<Dialogs::MainList*> list, bool sendReadReceipt) {
	auto mark = std::vector<not_null<History*>>();
	for (const auto &row : list->indexed()->all()) {
		if (const auto history = row->history()) {
			mark.push_back(history);
		}
	}
	for (const auto &history : mark) MarkAsReadThread(history, sendReadReceipt);
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

void MarkAsReadThread(not_null<Data::Thread*> thread, bool sendReadReceipt) {
	const auto readHistoryNative = [&](const not_null<History*> history)
	{
		history->owner().histories().readInbox(history, sendReadReceipt);
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
			forum->enumerateTopics([sendReadReceipt](
				not_null<Data::ForumTopic*> topic)
				{
					MarkAsReadThread(topic, sendReadReceipt);
				});
		} else if (const auto topic = thread->asTopic()) {
			topic->readTillEnd(sendReadReceipt);
		} else if (const auto history = thread->asHistory()) {
			readHistoryNative(history);
			if (const auto migrated = history->migrateSibling()) {
				readHistoryNative(migrated);
			}
		}
	}

	const auto history = thread->owningHistory();
	const auto rootId = thread->asTopic() ? thread->asTopic()->rootId() : MsgId();
	if (thread->unreadMentions().has()) {
		if (sendReadReceipt) sendReadMentions(thread);
		else history->clearUnreadMentionsFor(rootId);
	}

	if (thread->unreadReactions().has()) {
		if (sendReadReceipt) sendReadReactions(thread);
		else history->clearUnreadReactionsFor(rootId, thread->asSublist());
	}

	if (sendReadReceipt) LuxuryWorker::markAsOnline(&thread->session());
}

void readHistory(not_null<HistoryItem*> message) {
	const auto history = message->history();
	const auto tillId = message->id;

	history->session().data().histories().sendRequest(
		history,
		Data::Histories::RequestType::ReadInbox,
		[=](Fn<void()> finish) {
			const auto finished = [=] {
				LuxuryWorker::markAsOnline(&history->session());
				finish();
			};
			if (const auto channel = history->peer->asChannel()) {
				return history->session().api().request(MTPchannels_ReadHistory(
					channel->inputChannel(),
					MTP_int(tillId)
				)).done(finished).fail(finished).send();
			}
			return history->session().api().request(MTPmessages_ReadHistory(
				history->peer->input(),
				MTP_int(tillId)
			)).done([=](const MTPmessages_AffectedMessages &result) {
				history->session().api().applyAffectedMessages(history->peer, result);
				finished();
			}).fail([=] {
				LOG(("LuxuryGram: messages.readHistory failed for peer %1").arg(history->peer->id.value));
				finished();
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

QString getDCName(int dc) {
	const auto getName = [=]
	{
		switch (dc) {
			case 1:
			case 3: return tr::luxury_DCLocationMiami(tr::now);
			case 2:
			case 4: return tr::luxury_DCLocationAmsterdam(tr::now);
			case 5: return tr::luxury_DCLocationSingapore(tr::now);
			default: return QString();
		}
	};

	if (dc < 1) {
		return tr::luxury_UnknownDC(tr::now);
	}

	const auto name = getName();
	return name.isEmpty()
		? tr::luxury_UnknownDC(tr::now)
		: u"DC%1, %2"_q.arg(dc).arg(name);
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

namespace {

// Called for every message on layout, and the format only depends on the
// system locale, which does not change while we are running. Building a
// QLocale and asking it for the format each time was pure overhead.
const QLocale &systemLocale() {
	static const auto locale = QLocale();
	return locale;
}

const QString &shortTimeFormat() {
	static const auto result = systemLocale().timeFormat(QLocale::ShortFormat);
	return result;
}

const QString &secondsTimeFormat() {
	static const auto result = [] {
		const auto &shortFormat = shortTimeFormat();
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
	return result;
}

} // namespace

QString formatTimeWithSeconds(const QTime &time) {
	return systemLocale().toString(time, secondsTimeFormat());
}

QString formatDateTime(const QDateTime &date) {
	const auto datePart = systemLocale().toString(
		date.date(),
		QLocale::ShortFormat);

	return datePart + getLocalizedAt() + formatTimeWithSeconds(date.time());
}

QString formatMessageTime(const QTime &time) {
	return systemLocale().toString(
		time,
		LuxurySettings::getInstance().showMessageSeconds()
			? secondsTimeFormat()
			: shortTimeFormat());
}

int64 getMediaSizeBytes(not_null<HistoryItem*> message) {
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
				// shouldn't happen, but still
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
				// shouldn't happen, but still
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

int LuxuryMaxSelectedItems() {
	return LuxurySettings::getInstance().disableChatSelectionLimit()
		? std::numeric_limits<int>::max()
		: MaxSelectedItems;
}

int getScheduleTime(int64 sumSize) {
	auto time = 12;
	time += (int) std::ceil(std::max(6.0, std::ceil(sumSize / 1024.0 / 1024.0 * 0.7))) + 1;
	return time;
}

bool isMessageSavable(const not_null<HistoryItem*> item) {
	const auto &settings = LuxurySettings::getInstance();
	const auto peer = item->history()->peer;

	// First of all: under a passcode lock the client keeps receiving updates,
	// only UI is gated, so saving would continue silently. The toggle opts out
	// of that -- off means a delete arriving while locked is not recorded.
	if (Core::App().passcodeLocked()
		&& !settings.saveDeletedMessagesEvenWhenLocked()) {
		return false;
	}

	// The storage keys a row on its text and drops one that has none, so an item
	// with nothing to store must not be marked deleted either -- a marker with no
	// row behind it is exactly what "it saved nothing" looks like. Only a couple
	// of media types reach this: a gift box and a wallpaper have no notification
	// text to fall back to. originalText() returns a reference, and the second
	// call only happens once the first came back empty.
	if (item->originalText().empty() && item->notificationText().empty()) {
		return false;
	}

	// Watching a chat is a per-chat opt-in, so it overrides both switches below.
	// Otherwise a watched chat would fetch and keep the media and then throw away
	// the row that is supposed to point at it.
	if (settings.isWatched(getDialogIdFromPeer(peer))) {
		return true;
	}

	if (!settings.saveDeletedMessages()) {
		return false;
	}

	if (const auto possiblyBot = peer->asUser()) {
		return !possiblyBot->isBot() || settings.saveForBots();
	}
	return true;
}

void processMessageDelete(not_null<HistoryItem*> item) {
	if (!isMessageSavable(item)) {
		item->history()->owner().destroyMessageWithCacheCleanup(item);
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
		const QString &query,
		Main::Session *,
		const UsernameResolverCallback &callback) {
	LOG(("LuxuryGram: searchPeer cannot resolve query \"%1\": no network lookup exists").arg(query));
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
		if (text.length() <= maxLength) {
			return text;
		}
		return text.left(safeCutPosition(text, maxLength - 1))
			+ QChar(8230); // …
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

PeerData *getPeerFromDialogId(ID dialogId) {
	const auto bare = getBareDialogId(dialogId);
	if (!bare) {
		return nullptr;
	}
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto session = account->maybeSession()) {
			const auto &data = session->data();
			PeerData *from = nullptr;
			if (dialogId > 0) {
				from = data.userLoaded(UserId(bare));
			} else if (const auto channel = data.channelLoaded(ChannelId(bare))) {
				from = channel;
			} else {
				from = data.chatLoaded(ChatId(bare));
			}
			if (from) {
				return from;
			}
		}
	}

	return nullptr;
}

// A lone surrogate is sent by toUtf8() as '?' and drawn as a box, so a cut
// never splits an emoji or any other pair.
int safeCutPosition(const QString &text, int position) {
	return (position > 0
		&& position < text.size()
		&& text[position - 1].isHighSurrogate()
		&& text[position].isLowSurrogate())
		? (position - 1)
		: position;
}

QString filterZalgo(const QString &text) {
	static const auto regex = QRegularExpression(
		kZalgoPattern,
		QRegularExpression::UseUnicodePropertiesOption);

	// This runs for every author line and every message text while the setting
	// is on, and the answer is almost always "nothing to filter" -- which must
	// not cost a Unicode-property regex over the whole string. Nothing the
	// pattern matches lives below U+0300. A surrogate pair may carry a combining
	// mark that a per-QChar look cannot see, so those still go to the regex.
	auto suspicious = false;
	for (const auto ch : text) {
		const auto code = ch.unicode();
		if (code < 0x0300) {
			continue;
		} else if (ch.isSurrogate()
			|| (ch.category() == QChar::Mark_NonSpacing)
			|| (code == 0x061C)
			|| (code >= 0x200E && code <= 0x200F)
			|| (code >= 0x202A && code <= 0x202E)
			|| (code >= 0x2066 && code <= 0x2069)) {
			suspicious = true;
			break;
		}
	}
	if (!suspicious) {
		return text;
	}

	// hasNext() runs the first match itself, so this is one pass, not two.
	auto it = regex.globalMatch(text);
	if (!it.hasNext()) {
		return text;
	}

	QString output;
	output.reserve(text.length());
	auto lastEnd = 0;
	while (it.hasNext()) {
		const auto match = it.next();
		if (!IsZalgo(match.capturedView())) {
			continue;
		}
		output.append(text.mid(lastEnd, match.capturedStart() - lastEnd));
		output.append(QString(match.capturedLength(), QChar(0x2060)));
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
			if (!parsedDate.isValid()) {
				continue;
			}
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
	// Quick replies and welcome messages are stored, not sent: a schedule
	// date would file them as scheduled messages of the chat instead.
	// Self-destructing media cannot be scheduled.
	if (ghost.isUseScheduledMessages()
		&& !options.scheduled
		&& !options.shortcutId
		&& !options.welcomeTemplate
		&& !options.ttlSeconds) {
		const auto delay = Core::App().settings().proxy().isEnabled()
			? (delaySeconds * 6 + 4) / 5 //ceil(delaySeconds * 1.2)
			: delaySeconds;
		options.scheduled = base::unixtime::now() + delay;
	}
}

rpl::producer<> PhotoLoadUpdates(
		not_null<Main::Session*> session,
		not_null<PhotoData*> photo) {
	return rpl::merge(
		session->downloaderTaskFinished(),
		session->data().photoLoadProgress(
		) | rpl::filter([=](not_null<PhotoData*> updated) {
			return (updated == photo);
		}) | rpl::to_empty);
}

rpl::producer<> DocumentLoadUpdates(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document) {
	return rpl::merge(
		session->downloaderTaskFinished(),
		session->data().documentLoadProgress(
		) | rpl::filter([=](not_null<DocumentData*> updated) {
			return (updated == document);
		}) | rpl::to_empty);
}
