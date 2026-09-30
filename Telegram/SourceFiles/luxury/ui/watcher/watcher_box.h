// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "base/basic_types.h"

class PeerData;

namespace Window {

class SessionController;

} // namespace Window

namespace LuxuryWatcher {

// Opens the Watcher box: sessions and watched events for one peer, with
// tabs, one-click sorting, refresh and tracking settings.
void Show(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

}
