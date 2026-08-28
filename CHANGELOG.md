# Changelog

This file tracks changes specific to LuxuryGram. Historical Telegram Desktop and inherited AyuGram changes remain available in [`changelog.txt`](changelog.txt) and the Git history.

Releases are published on the [Releases page](https://github.com/GofMan5/LuxuryGram/releases) and tagged `luxury-v<version>`.

## 1.0.4

### Changed

- A watched chat now saves the message as well as its attachment. Watching used to keep the file and nothing else, so a deleted message in a watched chat left an attachment on disk that no deleted-messages view listed; the row is now written whatever the global "save deleted messages" switch says, and the chat's menu offers the deleted-messages view on that basis. Everything that decides whether a message is kept — the expiring-media notice, the one-time media that is not wiped when it burns, the download link that survives a deletion — now asks the same question, so those no longer disagree with what is actually stored.

### Fixed

- Fixed the stalls and crashes while stickers and custom emoji load. Every animated frame was rounded on the way to the screen, and rounding reads the pixels, which forces a full copy of the frame — so every animated sticker and custom emoji on screen deep-copied itself on every repaint. Lottie and alpha webm frames are already transparent in the corners and lose nothing by not being rounded.
- Fixed a crash from the rounded-corner masks. A mask is handed out as a raw pointer that a video frame request can carry to a decoding thread, and changing the bubble corner radius overwrote the entry that pointer referred to. Masks are now appended rather than replaced, and the corner radius slider has a fixed range, so the pool cannot grow without bound.
- Fixed a crash when a message scrolled out of view while its media was still loading: a media that did not report the change left the view registered as heavy, and the next sweep walked freed memory. Unloading no longer dispatches through the destroyed view to reach that one line.
- Fixed a crash on exit caused by the "no cover" music artwork, which was cached in a way that freed it after the graphics backend it belonged to was already gone.
- Fixed an animated frame being handed back as uninitialised memory when the frame arrived during shutdown, and fixed the same path assuming its allocation succeeded.
- Fixed one unreadable value in the settings file costing the whole file. Reading stops at the first value of an unexpected type, which left the defaults in memory, and the next save — which happens on nearly any interaction — wrote those defaults over everything else. A mismatched value is now dropped on its own, and a file that still cannot be read is moved aside as `.broken` instead of being overwritten.
- Fixed the proxy list hanging on "checking…" on a second account. A proxy that completes the connection and then never answers reports neither success nor failure, and the check had no time limit; it probes each account's own home data centre, which is why one account could hang while another resolved normally.
- Fixed a watched-chat download failing quietly taking the user's download folder setting with it, and putting a retry box in front of them for a download they never started.
- Fixed deleted messages not being saved at all in several cases: forum messages were stored without their topic and so were invisible in every topic view, an admin clearing a channel's history for everyone destroyed the loaded messages with no save at all, "delete all messages from this user" and "delete my messages" in a group saved on one branch and not on the other, and a message the storage would have refused was marked deleted on screen before it was refused.
- Fixed messages saved before this release being invisible in a forum topic. They were written without a topic and are now shown in every topic of that forum, which is the most a row that never recorded one allows. Clearing a single topic deliberately leaves them alone rather than removing them from the other topics; clearing the whole forum from the chat list removes them.
- Fixed a crash in the deleted-messages view when a message was clicked after the list had been rebuilt, and fixed the view holding on to a message rather than its id, which was a dangling pointer as soon as that message was destroyed.
- Fixed the last batch of deleted messages before a quit being dropped. Database work is posted and forgotten, and nothing waited for it; the quit now waits for that work to finish, and every statement carries its own lock timeout, so a database another process has locked fails the write rather than leaving a window-less process behind.
- Fixed a database error on the background thread terminating the application instead of being logged, and fixed a failed migration renaming the database aside and taking every saved message with it when the real problem was usually a full disk.
- Fixed a file still being downloaded when its message was deleted: the move that keeps it used to fail outright on Windows, so it is now retried when the download finishes, and retried on its own if nothing else is downloading.
- Fixed the theme preview re-reading the theme file on every download progress tick, the forward sync checking the size of a file on disk on every tick as well, and the invisible-character filter scanning every message twice.

## 1.0.3

### Added

- Added "Watch Media" to a chat's own menu. A watched chat downloads every attachment as it arrives, so a message deleted afterwards still has its file, and right-clicking that message in the deleted-messages view offers to show the attachment in its folder. It is per chat rather than global because watching costs traffic and disk on every message the chat receives, and the watched chats are listed in LuxuryGram settings once there is at least one. Fetched attachments waiting for a deletion are held under a 2 GB total and a 64 MB per-file budget, oldest discarded first; the ones kept for a deleted message are never discarded.
- Added a check-for-updates button to the About box, with the same status line as settings: checking, latest version installed, downloading with progress, ready to install, or failed. It also becomes the install button once a package is ready, so a check that goes nowhere says so instead of looking like nothing happened.

### Fixed

- Fixed automatic updates never reaching any client. The update feed named each package without a separator, so every client since 1.0.1 asked GitHub for `.../releases/download/updatestx64upd1000002` and got a 404, and outside the status line in settings nothing said the check had failed. The separator lives in the feed rather than in the application, so 1.0.1 and 1.0.2 installations pick this up as soon as this release's feed is published and can update themselves to 1.0.3.
- Fixed 1.0.2 refusing to start on an existing profile. It rewrote the whole deleted-messages table on the interface thread before any window appeared: on the reference machine that burned twenty minutes, grew the write-ahead log past 11 GB and took free disk space from 33.5 to 22.7 GiB, with nothing on screen to distinguish it from a hang.
- Fixed the upgrade from a database 1.0.2 had already started on deleting the stored message history outright to make the table match.
- Fixed the interface waiting on database work at startup: the database is now opened and migrated on its own thread, so a profile that needs a schema change shows its window immediately.
- Fixed joining a channel or group by invite link failing silently. During a rate limit the button did nothing at all, and every unrecognised failure was reported as "This invite link is invalid or has expired" — including that rate limit, which now says how long the wait is.
- Fixed opening an invite or folder link swallowing every failure that was not a bad request, and showing the rate-limit box behind the media viewer instead of in front of it.
- Fixed "delete my messages" reporting rate limits, fatal errors and completion only to the debug log, and claiming it was done after a run in which nothing was deleted.
- Fixed a failed send showing the raw `FLOOD_WAIT_86400` instead of a sentence, and fixed a rate-limit retry that could repeat once a second indefinitely.
- Fixed a long rate limit on sending looking like a message stuck with a sending clock: waits of half a minute or more are now reported, at most once a minute.

## 1.0.2

### Added

- Synchronized the codebase with Telegram Desktop 7.1.1, which brings welcome messages for groups and channels, buttons and file blocks in rich messages, video profile photos, a video editor with trimming and cover-frame selection, restoring of opened windows and chats on relaunch, music files in the attach menu, and the new WEB proxy type.
- Self-destruct timers and single view for photos and videos now render natively: a covered bubble, the one-time badge, and a countdown in the viewer. LuxuryGram keeps the media after it burns when "save deleted messages" is on, so an opened one-time photo or video stays readable instead of turning into an "expired" placeholder.

### Changed

- Reset settings now applies every setting whose effect needs more than a stored value, and names the ones only a restart can apply instead of reporting success either way.
- The colourful quotes and replies setting takes effect immediately; it previously needed a restart, because the choice was baked into each quote's paint cache.
- Screenshot protection, new in Telegram Desktop 7.1, never applies to a private chat: copying out of a one-to-one conversation is what this fork is for.

### Fixed

- Fixed crashes from five message context-menu actions that could outlive the message or the menu they belonged to.
- Fixed the message-shot box rendering messages that had been deleted while it was open, and fixed Save and Copy writing an empty file or wiping the clipboard when the image was rejected or the selection had gone.
- Opening the message-shot box no longer changes your colourful quotes and replies setting, which previously stayed changed if the box was closed in a way that skipped its cleanup.
- Fixed interface stalls from database work on the interface thread: the database is opened once instead of per statement, so no query pays for a write-ahead-log checkpoint, and the hidden-message cache is warmed on the database queue instead of by the first message repaint.
- Fixed the shadow-ban flag surviving into the next filters list you opened.
- Fixed a burnt one-time photo or video showing "expires in 0 seconds" in the viewer and waking the interface once a second for as long as it stayed open.
- Fixed a button-sized gap to the left of the message field when the attach button is hidden.
- Fixed the emoji button staying hidden after a bot keyboard closed, and reappearing when you had switched it off.
- Fixed a message shot of a timer photo keeping the countdown label after you switched dates off for shots.

## 1.0.1

### Added

- Added automatic updates served from this repository's releases. Packages are signed with the LuxuryGram update key and verified with RSA-4096 and SHA-256 before anything is unpacked.
- Added a message context-menu action that translates any selected message to the language configured in LuxuryGram settings.

### Changed

- Stripped the Linux release binaries: the download drops from about 93 MB to 65 MB and the unpacked application from 461 MB to roughly 277 MB.
- Synchronized the codebase with Telegram Desktop 7.0.9 while preserving LuxuryGram and AyuGram features.
- Published LuxuryGram forks for the updated `codegen`, `lib_ui`, `lib_tl`, and `lib_icu` submodules and restored Linux/Windows CI.
- Established the LuxuryGram project identity and public repository profile.
- Replaced inherited download and support links with verified LuxuryGram resources.
- Added contributor, conduct, security, support, legal, and pull-request guidance.
- Completed the LuxuryGram identity across executable names, desktop metadata, installers, bundle identifiers, crash UI, settings, and localization sources.
- Renamed inherited Ayu-prefixed application, language, codegen, and UI symbols while preserving compatibility-only storage paths, upstream services, and attribution.
- Renamed all project-owned `ayu` source, resource, QRC, style, and build paths to the canonical `luxury` namespace.

### Fixed

- Stopped offering to upload crash minidumps and reports to the upstream project's collector; reports stay on disk and can be viewed or saved.
- Stopped refetching artwork for saved music tracks that have none, which repeated two blocking network requests on every scroll pass, and limited how many cover lookups may run at once.
- Restored completion callbacks for custom and mixed forwarding, firing them once only after every chunk succeeds and suppressing them on cancellation, timeout, or missing media.
- Discarded stale delayed message-shot previews and avoided expensive pixel-diff buffers for images larger than four megapixels.
- Preserved protected polls, contacts, locations, dice, stories, and other non-file media as rich text during custom forwarding instead of treating them as missing files.
- Bounded decoded cover art and screenshot memory, reduced cover downloads to the requested resolution, and capped the cover cache by bytes instead of item count.
- Moved large filter imports off the UI thread into one rollback-safe transaction, replaced the handwritten UUID generator with Qt, and bounded remote profile collections and text.
- Preserved media-only deleted and edited history entries as localized text and retained sender peer types instead of silently dropping or misidentifying them.
- Bounded per-message filter memoization so long browsing sessions cannot grow the hidden-message cache indefinitely.
- Removed duplicate source text from the translation LRU and promoted cache hits in place instead of copying list nodes.
- Bounded per-account ghost settings, shadow-ban IDs, marks, font names, and saved theme titles from both files and live UI input.
- Prevented missed media-download completions and main-thread observer teardown races in custom forwarding.
- Snapshotted forwarding inputs on the main thread and resolved messages by stable IDs so background work no longer retains UI-owned message/media pointers.
- Prevented fast-send completion races, overlapping album sends, unsafe download filename collisions, and cross-account forward-state collisions.
- Isolated hidden-message, filter, ghost-mode, message-history, and message-shot state between production, test, and multi-account sessions.
- Made settings and language-cache writes atomic, contained language cache/CDN paths, and bounded bulk message deletion memory.
- Made SQLite recovery preserve the database, WAL, and SHM as one rollback-safe set and stopped failed migrations from being marked successful.
- Batched bulk deleted-message persistence into one SQLite transaction to prevent hundreds of synchronous disk commits from stalling the UI.
- Keyed translation results by the complete text, entities, languages, and provider so edits and provider changes cannot reuse stale output.
- Removed dangling delayed UI/config callbacks and initialized inherited data/UI state deterministically.
- Bounded translator responses and batches, serialized message-history storage, and hardened network/media lifetimes.
- Removed dead UI overloads that broke warning-as-error Linux builds and eliminated redundant settings option copies.

### Removed

- Removed the inherited remote-config lookups. The app no longer contacts `update.ayugram.one` or `api.exteragram.app` on start, and no longer shows those projects' developer and supporter badges.
- Removed the donation screens, which collected money for another project's author, and the `tg://support` link that opened them.

### Security

- Replaced the update signing key before the first release. An earlier build attached the signing tool to a workflow artifact with the private key compiled into it, and artifacts of a public repository are downloadable by anyone, so the key was treated as exposed.

