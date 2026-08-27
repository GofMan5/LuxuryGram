// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "luxury/data/entities.h"

namespace LuxuryFeatures::Watch {

// Fetches the media of a message in a watched chat as soon as the message
// arrives, so that a message deleted later still has its file. A no-op for
// everything else, and deliberately a cheap one: this runs for every incoming
// message in the app.
void processNewMessage(not_null<HistoryItem*> item);

// Moves whatever was fetched for this message out of the prunable area and gives
// back the name to remember with the deleted-message row. Empty when there is
// nothing to keep: media never fetched, still downloading, or pruned before the
// message was deleted.
[[nodiscard]] QString keepMediaForDeleted(not_null<HistoryItem*> item);

// DeletedMessage::mediaPath holds the bare name, not a path: the working
// directory moves with a portable build, and an absolute path would not survive
// it. The deleted-messages viewer never sees that column though -- the query it
// runs hands back the base class, without it -- so the file is found by the two
// things the viewer does have. Empty when nothing was kept for the message.
[[nodiscard]] QString keptFileForMessage(ID dialogId, MsgId messageId);

// True for the files this fetches on its own. A failure writing one of those is
// not the user's download failing: nobody asked for it, so it must not put a
// modal in front of them or be read as a problem with their download path.
[[nodiscard]] bool ownsFetchedPath(const QString &path);

} // namespace LuxuryFeatures::Watch
