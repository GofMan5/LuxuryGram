/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_history_messages.h"
#include "rpl/lifetime.h"

namespace HistoryView {

// Forward the model's owned text projection of one chat to the GPUI feed.
//
// The subscription lives on the model thread and is destroyed with `lifetime`.
// A null snapshot (lock, logout or account switch) revokes the feed; the
// caller clears it on chat switch by destroying the subscription. Nothing is
// sent until a GPUI history DLL was loaded by `TryStartGpuiHistory`, so the
// default client without the DLL is unaffected.
[[nodiscard]] bool TryStartGpuiHistory();

void ShowGpuiHistory(
	not_null<History*> history,
	rpl::lifetime &lifetime);

} // namespace HistoryView
