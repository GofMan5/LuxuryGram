#pragma once

#include "base/basic_types.h"

namespace Ui {
class RoundButton;
} // namespace Ui

namespace Luxury::Ui {

// RoundButton paints its disabled state exactly like the enabled one, so a
// box button that waits for content has to dim itself to stop looking broken.
void SetBoxButtonEnabled(not_null<::Ui::RoundButton*> button, bool enabled);

} // namespace Luxury::Ui
