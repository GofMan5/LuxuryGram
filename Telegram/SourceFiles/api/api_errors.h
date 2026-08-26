/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MTP {
class Error;
} // namespace MTP

namespace Api {

// Seconds the server wants us to wait, or 0 when the error is not a rate limit.
// FLOOD_WAIT_n, FLOOD_PREMIUM_WAIT_n, SLOWMODE_WAIT_n, PREMIUM_SUB_WAIT_n and
// TAKEOUT_INIT_DELAY_n all carry the count in the same place.
[[nodiscard]] int ErrorWaitSeconds(const QString &type);
[[nodiscard]] int ErrorWaitSeconds(const MTP::Error &error);

// The rate-limit sentence on its own, for the callers that know the wait but
// never see an error: the mtproto instance handles short flood waits itself and
// only publishes the seconds.
[[nodiscard]] QString FloodWaitText(int seconds);

// Something to show the user for an RPC error, for any handler that has nothing
// better. Never empty: an action that fails and says nothing is indistinguishable
// from an action that was never sent, which is the bug this exists to prevent.
// Rate limits come back with the wait spelled out, since "try again later" without
// a number is the complaint this answers.
[[nodiscard]] QString ErrorText(const QString &type);
[[nodiscard]] QString ErrorText(const MTP::Error &error);

} // namespace Api
