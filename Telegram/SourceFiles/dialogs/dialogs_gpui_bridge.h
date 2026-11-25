/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"

#include "rpl/lifetime.h"

class QWidget;

namespace Main {
class Session;
} // namespace Main

namespace Data {
class Folder;
} // namespace Data

namespace Dialogs {

// Forward the chats list of one session to the GPUI chat list.
//
// The subscription lives on the model thread and is destroyed with `lifetime`.
// A destroyed subscription revokes the list; the caller clears it on lock or
// account switch by destroying it. Nothing is sent until a GPUI history DLL
// was loaded by `HistoryView::TryStartGpuiHistory`, so the default client
// without the DLL is unaffected.
void ShowGpuiDialogs(
	not_null<Main::Session*> session,
	Data::Folder *folder,
	rpl::lifetime &lifetime);

// Let the GPUI list open chats through the caller's own path.
//
// `open` runs on the main thread with the chosen chat's peer id, like every
// other navigation in the client; the caller resolves it against its own
// session, so a peer that was gone by then simply does not open. The
// returned lifetime owns the registration: destroying it makes the list
// stop opening chats before `open` could dangle.
[[nodiscard]] rpl::lifetime EnableGpuiDialogsOpen(Fn<void(PeerId)> open);

// Close the GPUI list and drop its rows, on lock, logout or account switch.
// The next `ShowGpuiDialogs` opens it again.
void CloseGpuiDialogs();

// Keeps the GPUI chat list window exactly over the Qt chats column, so the
// list is the chats surface while the Qt frame still owns the main loop.
//
// The host widget's own list stays hidden while the surface is visible.
// Destroying the surface hides the list window and releases the handle.
class GpuiDialogsSurface {
public:
	explicit GpuiDialogsSurface(not_null<QWidget*> area);
	~GpuiDialogsSurface();

	GpuiDialogsSurface(const GpuiDialogsSurface &) = delete;
	GpuiDialogsSurface &operator=(const GpuiDialogsSurface &) = delete;

	// Bring the window to the area's current rectangle and visibility.
	void sync();

	// Hide the list and forget the handle, e.g. on lock or account switch.
	void release();

	[[nodiscard]] bool active() const;

private:
	const not_null<QWidget*> _area;
	rpl::lifetime _lifetime;
	void *_window = nullptr;

};

} // namespace Dialogs
