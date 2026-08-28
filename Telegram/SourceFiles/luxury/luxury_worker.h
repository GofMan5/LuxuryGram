// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "window/window_session_controller.h"

namespace LuxuryWorker {

void markAsOnline(not_null<Main::Session*> session);

// Re-arms the poll. The poll stops itself once no account owes an offline
// packet, so a setting that creates one with no event behind it has to say so.
void wake();

void initialize();

}
