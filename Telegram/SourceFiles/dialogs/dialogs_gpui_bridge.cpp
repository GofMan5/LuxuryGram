/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_gpui_bridge.h"

#include "history/view/history_view_gpui_bridge.h"
#include "history/history.h"
#include "history/history_item.h"

#include "data/data_changes.h"
#include "data/data_folder.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"

#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"

#include "main/main_session.h"
#include "ui/ui_utility.h"

#include <QtCore/QDateTime>
#include <QtCore/QEvent>
#include <QtCore/QLocale>
#include <QtCore/QPoint>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#include "base/event_filter.h"
#include "base/platform/win/base_windows_safe_library.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#endif // Q_OS_WIN

namespace Dialogs {
namespace {

#ifdef Q_OS_WIN

// Only one chats column may own the single list window: the most recently
// shown one, as with the history feed.
GpuiDialogsSurface **ActiveSurfaceSlot() {
	static GpuiDialogsSurface *active = nullptr;
	return &active;
}

// Mirrors `DialogRowAbi` in rust/apps/luxurygram-shell/src/history_bridge.rs.
struct RowAbi {
	uint64_t peer;
	uint32_t unread;
	uint8_t flags;
	const char *title;
	size_t title_len;
	const char *subtitle;
	size_t subtitle_len;
	const char *time;
	size_t time_len;
};

constexpr auto kDllName = "luxurygram_shell.dll";
constexpr auto kPinned = uint8_t(1);
constexpr auto kMuted = uint8_t(2);
constexpr auto kOnline = uint8_t(4);
constexpr auto kOutgoing = uint8_t(8);

using Open = int (*)();
using Apply = int (*)(const RowAbi *, size_t);
using Close = int (*)();
using Window = std::intptr_t (*)();
using OpenCallback = int (*)(void *context, uint64_t peer);
using SetOpenCallback = int (*)(OpenCallback, void *);

Open OpenFn = nullptr;
Apply ApplyFn = nullptr;
Close CloseFn = nullptr;
Window WindowFn = nullptr;
SetOpenCallback SetOpenCallbackFn = nullptr;

// The single open destination. Written on the main thread, read on the GPUI
// thread inside the callback, so it is kept behind a mutex. The generation
// identifies the registration an open was accepted for, so a column that was
// switched away, locked or destroyed in between never receives it.
std::mutex OpenMutex;
Fn<void(PeerId)> OpenHandler;
uint64_t OpenGeneration = 0;

int OnDialogOpen(void *context, uint64_t peer) {
	std::unique_lock lock(OpenMutex);
	if (!OpenHandler) {
		return 0;
	}
	const auto generation = OpenGeneration;
	auto handler = OpenHandler;
	lock.unlock();
	// The handler resolves the peer itself on the main thread: the GPUI side
	// knows only the id, and a peer that was gone by then simply does not open.
	crl::on_main([=] {
		std::unique_lock inner(OpenMutex);
		if (generation != OpenGeneration) {
			return;
		}
		inner.unlock();
		handler(PeerId(peer));
	});
	return 1;
}

// Make the list stop opening chats, unless a newer registration owns it.
void ReleaseOpen(uint64_t generation) {
	const std::lock_guard lock(OpenMutex);
	if (generation == OpenGeneration) {
		OpenHandler = nullptr;
	}
}

// The DLL is optional: the client without it behaves exactly as before. The
// history bridge loads the same library and starts the GPUI thread; this
// only resolves the chat-list entry points.
bool LoadBridge() {
	static const auto loaded = [] {
		const auto library = base::Platform::SafeLoadLibrary(
			QDir(QCoreApplication::applicationDirPath())
				.filePath(QString::fromLatin1(kDllName))
				.toStdWString()
				.c_str());
		return base::Platform::LoadMethod(
				library,
				"luxurygram_dialogs_open",
				OpenFn)
			&& base::Platform::LoadMethod(
				library,
				"luxurygram_dialogs_apply",
				ApplyFn)
			&& base::Platform::LoadMethod(
				library,
				"luxurygram_dialogs_close",
				CloseFn)
			&& base::Platform::LoadMethod(
				library,
				"luxurygram_dialogs_window",
				WindowFn)
			&& base::Platform::LoadMethod(
				library,
				"luxurygram_dialogs_set_open_callback",
				SetOpenCallbackFn);
	}();
	return loaded;
}

// Text of the chat's last message the way the Qt row shows it: the sender
// in groups and channels, then the notification text.
[[nodiscard]] std::string LastMessageText(not_null<History*> history) {
	const auto item = history->lastMessage();
	if (!item) {
		return {};
	}
	const auto text = item->notificationText().text;
	const auto from = item->from();
	const auto showSender = from
		&& !history->peer->isUser()
		&& !history->peer->isMegagroup();
	auto result = showSender ? (from->name() + ": ") : QString();
	result += text;
	return result.toStdString();
}

[[nodiscard]] std::string LastMessageTime(not_null<History*> history) {
	const auto item = history->lastMessage();
	if (!item) {
		return {};
	}
	const auto date = QDateTime::fromSecsSinceEpoch(item->date());
	return QLocale().toString(date, QLocale::ShortFormat).toStdString();
}

// One row in display order. Strings are owned by the row and read by the
// bridge during `ApplyFn`, so they stay alive until it returns.
struct Row {
	std::string title;
	std::string subtitle;
	std::string time;
	RowAbi abi;
};

// The whole list in the order the Qt list paints it: fixed-on-top entries,
// then pinned, then the rest by date, as `IndexedList` keeps them.
[[nodiscard]] std::vector<Row> BuildRows(not_null<MainList*> list) {
	std::vector<Row> rows;
	const auto indexed = list->indexed();
	rows.reserve(indexed->size());
	for (const auto &row : *indexed) {
		const auto entry = row->entry();
		const auto history = row->key().history();
		if (!history) {
			continue;
		}
		const auto peer = history->peer;
		const auto unread = entry->chatListUnreadState();
		auto flags = uint8_t(0);
		if (entry->isPinnedDialog(0)) {
			flags |= kPinned;
		}
		if (history->muted()) {
			flags |= kMuted;
		}
		if (const auto user = peer->asUser(); user && Data::IsUserOnline(user)) {
			flags |= kOnline;
		}
		if (const auto item = history->lastMessage(); item && item->out()) {
			flags |= kOutgoing;
		}
		const auto shownUnread = std::max(unread.messages, unread.marks);
		auto title = peer->name().toStdString();
		auto subtitle = LastMessageText(history);
		auto time = LastMessageTime(history);
		RowAbi abi{
			.peer = peer->id.value,
			.unread = uint32_t(std::clamp(shownUnread, 0, 9999)),
			.flags = flags,
			.title = title.data(),
			.title_len = title.size(),
			.subtitle = subtitle.data(),
			.subtitle_len = subtitle.size(),
			.time = time.data(),
			.time_len = time.size(),
		};
		rows.push_back({
			.title = std::move(title),
			.subtitle = std::move(subtitle),
			.time = std::move(time),
			.abi = abi,
		});
	}
	return rows;
}

// Every signal that moves a row, renames a chat or changes a counter lands
// here within one event loop turn, so a burst of updates costs one snapshot.
// Without a surface the list window is closed, and a closed list drops what
// it is sent, so nothing is built or copied until one shows it.
void SchedulePush(not_null<MainList*> list) {
	if (!WindowFn()) {
		return;
	}
	Ui::PostponeCall([=] {
		if (!WindowFn()) {
			return;
		}
		std::vector<RowAbi> abis;
		const auto rows = BuildRows(list);
		abis.reserve(rows.size());
		for (const auto &row : rows) {
			abis.push_back(row.abi);
		}
		ApplyFn(abis.data(), abis.size());
	});
}

// Chats lists that feed the GPUI list. A surface that opens the list window
// asks each of them for a snapshot, since rows sent while the window was
// closed were dropped. Each subscription removes itself in its lifetime.
std::mutex RequestersMutex;
std::vector<std::pair<uint64_t, Fn<void()>>> SnapshotRequesters;
uint64_t SnapshotRequesterGeneration = 0;

// Register `request` to be asked for a snapshot whenever the list window
// opens, and ask now in case it already is open.
void RegisterSnapshotRequester(Fn<void()> request, rpl::lifetime &lifetime) {
	auto id = uint64_t(0);
	{
		const std::lock_guard lock(RequestersMutex);
		id = ++SnapshotRequesterGeneration;
		SnapshotRequesters.emplace_back(id, std::move(request));
	}
	lifetime.add([id] {
		const std::lock_guard lock(RequestersMutex);
		SnapshotRequesters.erase(
			std::remove_if(
				SnapshotRequesters.begin(),
				SnapshotRequesters.end(),
				[id](const auto &entry) { return entry.first == id; }),
			SnapshotRequesters.end());
	});
}

void RequestSnapshots() {
	std::vector<Fn<void()>> copy;
	{
		const std::lock_guard lock(RequestersMutex);
		for (const auto &[id, request] : SnapshotRequesters) {
			copy.push_back(request);
		}
	}
	for (const auto &request : copy) {
		request();
	}
}

#endif // Q_OS_WIN

} // namespace

void ShowGpuiDialogs(
		[[maybe_unused]] not_null<Main::Session*> session,
		[[maybe_unused]] Data::Folder *folder,
		[[maybe_unused]] rpl::lifetime &lifetime) {
#ifdef Q_OS_WIN
	if (!HistoryView::TryStartGpuiHistory() || !LoadBridge()) {
		return;
	}
	const auto list = session->data().chatsList(folder);
	// Rows flow while the list window is shown: a closed list drops what it
	// is sent, so nothing is built or copied until a surface shows it, and
	// the surface then asks every subscribed list for a fresh snapshot.
	RegisterSnapshotRequester([=] {
		SchedulePush(list);
	}, lifetime);
	session->data().chatsListChanges(
	) | rpl::on_next([=](Data::Folder *changed) {
		if (changed == folder) {
			SchedulePush(list);
		}
	}, lifetime);
	session->data().chatsListLoadedEvents(
	) | rpl::on_next([=](Data::Folder *changed) {
		if (changed == folder) {
			SchedulePush(list);
		}
	}, lifetime);
	list->unreadStateChanges(
	) | rpl::on_next([=] {
		SchedulePush(list);
	}, lifetime);
	list->fullSize().changes(
	) | rpl::on_next([=] {
		SchedulePush(list);
	}, lifetime);
	session->changes().historyUpdates(
		Data::HistoryUpdate::Flag::IsPinned
		| Data::HistoryUpdate::Flag::UnreadView
		| Data::HistoryUpdate::Flag::UnreadMentions
		| Data::HistoryUpdate::Flag::UnreadReactions
		| Data::HistoryUpdate::Flag::OutboxRead
		| Data::HistoryUpdate::Flag::MessageSent
		| Data::HistoryUpdate::Flag::ClientSideMessages
		| Data::HistoryUpdate::Flag::Folder
	) | rpl::on_next([=] {
		SchedulePush(list);
	}, lifetime);
	session->changes().peerUpdates(
		Data::PeerUpdate::Flag::Name
		| Data::PeerUpdate::Flag::OnlineStatus
	) | rpl::on_next([=] {
		SchedulePush(list);
	}, lifetime);
	session->changes().messageUpdates(
		Data::MessageUpdate::Flag::DialogRowRefresh
	) | rpl::on_next([=] {
		SchedulePush(list);
	}, lifetime);
#endif // Q_OS_WIN
}

rpl::lifetime EnableGpuiDialogsOpen(Fn<void(PeerId)> open) {
	rpl::lifetime result;
#ifdef Q_OS_WIN
	if (!open) {
		return result;
	}
	if (!HistoryView::TryStartGpuiHistory() || !LoadBridge()) {
		return result;
	}
	static const auto registered = [] {
		return SetOpenCallbackFn(&OnDialogOpen, nullptr) == 0;
	}();
	if (!registered) {
		return result;
	}
	uint64_t generation = 0;
	{
		const std::lock_guard lock(OpenMutex);
		generation = ++OpenGeneration;
		OpenHandler = std::move(open);
	}
	result.add([=] {
		ReleaseOpen(generation);
	});
#endif // Q_OS_WIN
	return result;
}

void CloseGpuiDialogs() {
#ifdef Q_OS_WIN
	if (LoadBridge()) {
		CloseFn();
	}
#endif // Q_OS_WIN
}

GpuiDialogsSurface::GpuiDialogsSurface(not_null<QWidget*> area)
: _area(area) {
#ifdef Q_OS_WIN
	if (!HistoryView::TryStartGpuiHistory() || !LoadBridge()) {
		return;
	}
	// The handle is published on the GPUI thread right after the window opens,
	// so it may still be zero when the column is first shown.
	base::install_event_filter(_area, [=](not_null<QEvent*> event) {
		switch (event->type()) {
		case QEvent::Resize:
		case QEvent::Move:
		case QEvent::Show:
		case QEvent::Hide:
		case QEvent::WindowStateChange:
			sync();
			break;
		default:
			break;
		}
		return base::EventFilterResult::Continue;
	}, _lifetime);
	sync();
#endif // Q_OS_WIN
}

GpuiDialogsSurface::~GpuiDialogsSurface() {
	release();
}

bool GpuiDialogsSurface::active() const {
#ifdef Q_OS_WIN
	return _window != nullptr;
#else
	return false;
#endif
}

void GpuiDialogsSurface::release() {
#ifdef Q_OS_WIN
	if (*ActiveSurfaceSlot() == this) {
		*ActiveSurfaceSlot() = nullptr;
	}
	if (_window) {
		ShowWindow(static_cast<HWND>(_window), SW_HIDE);
	}
	_window = nullptr;
	CloseFn();
#endif // Q_OS_WIN
}

void GpuiDialogsSurface::sync() {
#ifdef Q_OS_WIN
	if (!_window) {
		// Ask for the list window; asking again while it opens is a no-op.
		OpenFn();
		_window = reinterpret_cast<void*>(WindowFn());
		if (!_window) {
			return;
		}
		// Rows sent while the window was closed were dropped.
		RequestSnapshots();
	}
	const auto window = static_cast<HWND>(_window);
	const auto visible = _area->isVisible()
		&& !_area->window()->isMinimized();
	if (!visible) {
		if (*ActiveSurfaceSlot() == this) {
			ShowWindow(window, SW_HIDE);
		}
		return;
	}
	*ActiveSurfaceSlot() = this;
	// The list covers the area in screen coordinates, like a native child.
	const auto origin = _area->mapToGlobal(QPoint());
	const auto size = _area->size();
	SetWindowPos(
		window,
		HWND_TOP,
		origin.x(),
		origin.y(),
		std::max(size.width(), 1),
		std::max(size.height(), 1),
		SWP_NOACTIVATE | SWP_SHOWWINDOW);
#endif // Q_OS_WIN
}

} // namespace Dialogs
