/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_history_messages.h"
#include "rpl/lifetime.h"

class QWidget;

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

// Keeps the GPUI feed window exactly over the Qt message area, so the feed is
// the chat surface while the Qt frame still owns the main loop.
//
// gpui-ce 0.3.0 creates top-level windows only (no child or owned HWND), so
// the surface tracks the native rectangle with SetWindowPos on every move or
// resize of the host widget, and shows or hides with it. The host widget's own
// list stays hidden while the surface is visible. Destroying the surface hides
// the feed window and releases the handle; the bridge outlives it.
class GpuiHistorySurface {
public:
	explicit GpuiHistorySurface(not_null<QWidget*> area);
	~GpuiHistorySurface();

	GpuiHistorySurface(const GpuiHistorySurface &) = delete;
	GpuiHistorySurface &operator=(const GpuiHistorySurface &) = delete;

	// Bring the window to the area's current rectangle and visibility.
	void sync();

	// Hide the feed and forget the handle, e.g. on chat switch or lock.
	void release();

	[[nodiscard]] bool active() const;

private:
	const not_null<QWidget*> _area;
	rpl::lifetime _lifetime;
	void *_window = nullptr;

};

} // namespace HistoryView
