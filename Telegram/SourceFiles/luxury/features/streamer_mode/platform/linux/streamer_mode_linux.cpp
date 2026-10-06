// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026

#include "luxury/features/streamer_mode/platform/linux/streamer_mode_linux.h"

#include "base/debug_log.h"

namespace LuxuryFeatures::StreamerMode::Platform {

// ponytail: Linux capture exclusion needs per-shell hooks (KDE
// _KDE_NET_WM_WINDOW_CAST_SKIP and friends) that stay unwired until asked for.
void SetWindowCaptureExcluded(not_null<QWidget*>, bool excluded) {
	static bool logged = false;
	if (excluded && !logged) {
		logged = true;
		LOG(("StreamerMode: window capture exclusion is not implemented on this platform"));
	}
}

} // namespace LuxuryFeatures::StreamerMode::Platform
