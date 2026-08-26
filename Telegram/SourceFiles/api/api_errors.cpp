/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_errors.h"

#include "lang/lang_keys.h"
#include "mtproto/mtproto_response.h"
#include "ui/text/format_values.h"

namespace Api {

int ErrorWaitSeconds(const QString &type) {
	static const auto RegExp = QRegularExpression(u"^("
		"FLOOD_WAIT|"
		"FLOOD_PREMIUM_WAIT|"
		"SLOWMODE_WAIT|"
		"PREMIUM_SUB_WAIT|"
		"TAKEOUT_INIT_DELAY"
		")_(\\d+)$"_q);
	const auto match = RegExp.match(type);
	return match.hasMatch() ? match.captured(2).toInt() : 0;
}

int ErrorWaitSeconds(const MTP::Error &error) {
	return ErrorWaitSeconds(error.type());
}

QString FloodWaitText(int seconds) {
	// Parenthesised rather than woven into a sentence because there is no
	// phrase for "retry in {duration}" in the language files, and inventing one
	// means shipping it untranslated in every language but two. FormatMuteFor
	// picks seconds, minutes or a days/hours/minutes breakdown, so a 24-hour
	// flood wait does not read as "86400".
	return tr::lng_flood_error(tr::now)
		+ u" ("_q
		+ Ui::FormatMuteFor(seconds)
		+ ')';
}

QString ErrorText(const QString &type) {
	if (const auto seconds = ErrorWaitSeconds(type)) {
		return FloodWaitText(seconds);
	} else if (type == u"CHANNEL_PRIVATE"_q
		|| type == u"CHANNEL_PUBLIC_GROUP_NA"_q
		|| type == u"USER_BANNED_IN_CHANNEL"_q) {
		return tr::lng_group_not_accessible(tr::now);
	} else if (type == u"INVITE_HASH_EXPIRED"_q
		|| type == u"INVITE_HASH_INVALID"_q) {
		// One phrase for both: it already says "invalid or has expired", and the
		// server does not consistently distinguish the two anyway.
		return tr::lng_group_invite_bad_link(tr::now);
	} else if (type == u"USERS_TOO_MUCH"_q) {
		return tr::lng_group_full(tr::now);
	}
	// PEER_FLOOD and the premium-required errors are deliberately absent: their
	// real text needs a Session to build a spambot or subscription link, and the
	// call sites that can do that already do. Here they fall through.
	//
	// Last resort, and deliberately not silence: the raw type is ugly but it is
	// searchable, and upstream shows it the same way from local_url_handlers.
	return u"Error: "_q + type;
}

QString ErrorText(const MTP::Error &error) {
	return ErrorText(error.type());
}

} // namespace Api
