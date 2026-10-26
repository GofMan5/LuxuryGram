// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026

#include "luxury/features/streamer_mode/platform/win/streamer_mode_win.h"

#include <QtWidgets/QWidget>

#include <windows.h>

namespace LuxuryFeatures::StreamerMode::Platform {

void SetWindowCaptureExcluded(
		not_null<QWidget*> widget,
		bool excluded) {
	const auto handle = reinterpret_cast<HWND>(widget->winId());
	const auto applied = SetWindowDisplayAffinity(
		handle,
		excluded ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
	if (!applied && excluded) {
		// Windows 10 before 2004 has no EXCLUDEFROMCAPTURE: the window then
		// shows black in captures instead of leaking its content.
		SetWindowDisplayAffinity(handle, WDA_MONITOR);
	}
}

} // namespace LuxuryFeatures::StreamerMode::Platform
