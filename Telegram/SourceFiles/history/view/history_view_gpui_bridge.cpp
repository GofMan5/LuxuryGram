/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_gpui_bridge.h"

#include "history/history.h"

#include "data/data_messages.h"

#include <QtCore/QEvent>
#include <QtCore/QPoint>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#ifdef Q_OS_WIN
#include "base/event_filter.h"
#include "base/platform/win/base_windows_safe_library.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#endif // Q_OS_WIN

namespace HistoryView {
namespace {

#ifdef Q_OS_WIN

// Only one chat area may own the single feed window: the most recently shown
// one. A ChatWidget for a reply thread, a topic or a separate window that is
// hidden or switched away releases it, so the feed never covers two areas.
GpuiHistorySurface **ActiveSurfaceSlot() {
	static GpuiHistorySurface *active = nullptr;
	return &active;
}

// Mirrors `HistoryRowAbi` in rust/apps/luxurygram-shell/src/history_bridge.rs.
struct RowAbi {
	uint64_t peer;
	int64_t message;
	uint32_t date;
	uint8_t outgoing;
	const char *sender;
	size_t sender_len;
	const char *text;
	size_t text_len;
};

// Mirrors `HistoryRekeyAbi`.
struct RekeyAbi {
	uint64_t old_peer;
	int64_t old_message;
	uint64_t new_peer;
	int64_t new_message;
};

constexpr auto kDllName = "luxurygram_shell.dll";
constexpr auto kWindowCount = 50;

using Start = int (*)();
using Apply = int (*)(const RowAbi *, size_t, const RekeyAbi *, size_t);
using Clear = int (*)();
using Window = std::intptr_t (*)();

Start StartFn = nullptr;
Apply ApplyFn = nullptr;
Clear ClearFn = nullptr;
Window WindowFn = nullptr;

// The DLL is optional: the client without it behaves exactly as before.
bool LoadBridge() {
	static const auto loaded = [] {
		const auto library = base::Platform::SafeLoadLibrary(
			QDir(QCoreApplication::applicationDirPath())
				.filePath(QString::fromLatin1(kDllName))
				.toStdWString()
				.c_str());
		return base::Platform::LoadMethod(
				library,
				"luxurygram_history_start",
				StartFn)
			&& base::Platform::LoadMethod(
				library,
				"luxurygram_history_apply",
				ApplyFn)
			&& base::Platform::LoadMethod(
				library,
				"luxurygram_history_clear",
				ClearFn)
			&& base::Platform::LoadMethod(
				library,
				"luxurygram_history_window",
				WindowFn);
	}();
	return loaded;
}

#else // Q_OS_WIN

bool LoadBridge() {
	return false;
}

#endif // Q_OS_WIN

} // namespace

bool TryStartGpuiHistory() {
	if (!LoadBridge()) {
		return false;
	}
#ifdef Q_OS_WIN
	static const auto started = (StartFn() == 0);
	return started;
#else
	return false;
#endif
}

void ShowGpuiHistory(
		[[maybe_unused]] not_null<History*> history,
		[[maybe_unused]] rpl::lifetime &lifetime) {
	if (!TryStartGpuiHistory()) {
		return;
	}
#ifdef Q_OS_WIN
	const auto around = Data::MessagePosition();
	Data::HistoryTextViewer(history, around, kWindowCount, kWindowCount)
	| rpl::on_next([](std::optional<Data::HistoryTextSlice> slice) {
		if (!slice) {
			ClearFn();
			return;
		}
		std::vector<RowAbi> rows;
		rows.reserve(slice->rows.size());
		for (const auto &row : slice->rows) {
			rows.push_back({
				.peer = row.id.peer.value,
				.message = row.id.msg.bare,
				.date = uint32_t(row.date),
				.outgoing = row.outgoing ? uint8_t(1) : uint8_t(0),
				.sender = row.sender.c_str(),
				.sender_len = row.sender.size(),
				.text = row.text.c_str(),
				.text_len = row.text.size(),
			});
		}
		std::vector<RekeyAbi> rekeys;
		rekeys.reserve(slice->rekeys.size());
		for (const auto &rekey : slice->rekeys) {
			rekeys.push_back({
				.old_peer = rekey.oldId.peer.value,
				.old_message = rekey.oldId.msg.bare,
				.new_peer = rekey.newId.peer.value,
				.new_message = rekey.newId.msg.bare,
			});
		}
		ApplyFn(rows.data(), rows.size(), rekeys.data(), rekeys.size());
	}, lifetime);
#endif // Q_OS_WIN
}

GpuiHistorySurface::GpuiHistorySurface(not_null<QWidget*> area)
: _area(area) {
#ifdef Q_OS_WIN
	if (!TryStartGpuiHistory()) {
		return;
	}
	// The handle is published on the GPUI thread right after the window opens,
	// so it may still be zero when the first chat is shown.
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

GpuiHistorySurface::~GpuiHistorySurface() {
	release();
}

bool GpuiHistorySurface::active() const {
#ifdef Q_OS_WIN
	return _window != nullptr;
#else
	return false;
#endif
}

void GpuiHistorySurface::release() {
#ifdef Q_OS_WIN
	if (*ActiveSurfaceSlot() == this) {
		*ActiveSurfaceSlot() = nullptr;
	}
	if (_window) {
		ShowWindow(static_cast<HWND>(_window), SW_HIDE);
	}
	_window = nullptr;
	ClearFn();
#endif // Q_OS_WIN
}

void GpuiHistorySurface::sync() {
#ifdef Q_OS_WIN
	if (!_window) {
		_window = reinterpret_cast<void*>(WindowFn());
		if (!_window) {
			return;
		}
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
	// The feed covers the area in screen coordinates, like a native child.
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

} // namespace HistoryView
