// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

namespace LuxuryFeatures::MessageShot {

// Split out of message_shot.h so paint code can ask these without pulling in the
// history and window layers that ShotConfig needs.

// True only while a shot is being rendered into its own image.
[[nodiscard]] bool isTakingShot();

// Whether quotes and replies should be drawn flat: no tinted background, no
// background emoji. A shot answers from its own preference, everything else from
// the persisted setting -- which is why the shot box does not have to flip that
// setting for the whole app while it is open.
[[nodiscard]] bool simpleQuotesAndReplies();

} // namespace LuxuryFeatures::MessageShot
