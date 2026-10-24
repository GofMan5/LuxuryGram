// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "base/basic_types.h"

class QWidget;

namespace LuxuryFeatures::StreamerMode {

void apply(bool enabled);
void hideWidgetWindow(QWidget *widget);
void showWidgetWindow(QWidget *widget);

// Telegram's screenshot protection drives the same per-window capture
// exclusion. It reports every change here and applies the result, so lifting
// either one does not uncover a window the other still hides.
[[nodiscard]] bool excludedWithProtection(
	not_null<QWidget*> window,
	bool protection);

} // namespace LuxuryFeatures::StreamerMode
