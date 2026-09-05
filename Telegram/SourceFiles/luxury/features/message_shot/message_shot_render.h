// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

class PeerData;

namespace LuxuryFeatures::MessageShot {

// Split out of message_shot.h so paint code can ask these without pulling in the
// history and window layers that ShotConfig needs.

// Set only while a shot is being rendered. Exposed so the hooks that sit on hot
// paths -- PeerData::name() runs for every row of every dialogs repaint -- can
// branch on it without a call into another translation unit.
extern bool takingShot;

// True only while a shot is being rendered into its own image.
[[nodiscard]] inline bool isTakingShot() {
	return takingShot;
}

// Whether quotes and replies should be drawn flat: no tinted background, no
// background emoji. A shot answers from its own preference, everything else from
// the persisted setting -- which is why the shot box does not have to flip that
// setting for the whole app while it is open.
[[nodiscard]] bool simpleQuotesAndReplies();

// While an anonymous shot is being rendered, the pseudonym that stands in for
// this peer -- numbered in the order the shot first meets it. nullptr the rest of
// the time, and whenever the option is off, so every other caller keeps the real
// name. The pointee stays put until the shot ends.
[[nodiscard]] const QString *AnonymousName(not_null<const PeerData*> peer);

// True only while a shot with the anonymous option on is being rendered. For the
// things that name a sender without going through its name: an avatar, an emoji
// status, a signature.
//
// Known ceiling: a service message ("N pinned a message") has its text composed
// once, when the message arrives, so it keeps the real names. Covering it would
// mean regenerating that text for the shot and putting it back afterwards.
[[nodiscard]] bool isAnonymousShot();

} // namespace LuxuryFeatures::MessageShot
