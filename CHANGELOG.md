# Changelog

This file tracks changes specific to LuxuryGram. Historical Telegram Desktop and inherited AyuGram changes remain available in [`changelog.txt`](changelog.txt) and the Git history.

Releases are published on the [Releases page](https://github.com/GofMan5/LuxuryGram/releases) and tagged `luxury-v<version>`.

## Unreleased

### Changed

- Watcher reaction cards now show what was reacted to: the counted emoji summary on the first line and a preview of the message text on the second.
- Watcher edit cards collapse all revisions of one message into a single card that shows the full arc — "Was: original text / Now: current text" — instead of one card per revision; the Events counts follow the grouping.
- Collapsed event cards no longer cut texts at 160 characters: texts show in full, and only a true wall of text (over 2000 characters) stays folded behind the expand chevron.

## 1.0.17

### Changed

- The Watcher got a full visual pass: a fixed neon-azure accent now drives the active tab pill (soft glow plus a rimmed outline), the sort control, the online dot and its pulse, duration pills, card hover rims, chip outlines and the header hairline; tabs and the sort control sit in recessed dark tracks. Event cards use a cleaner vertical layout (chip and date on one top row, full-width text below, a chevron marking the cards that expand), and timestamps are compact — bare time for today, day and month within the year.

## 1.0.16

### Added

- The Watcher now records message reactions: every genuine change of a message's reaction state appears in the Events timeline as a pink "Reaction" card with the counted emoji summary (initial syncs don't count, only live changes).
- Event cards with long text (deleted and edited messages) can be clicked to expand the full text and clicked again to collapse; the cards below move out of the way.

### Fixed

- Popup menus lost hover and clicks on pointer stacks that route popup-bound mouse input unreliably: items did not highlight under the cursor, and a press closed the menu without acting. Menus now grab the mouse for the whole time they are open (the native-menu pattern), so every mouse message is delivered to the menu itself; a submenu hands the grab back to its parent when it closes.

## 1.0.15

### Changed

- The Watcher box is rebuilt around a pinned header: the tab bar and the toolbar stay in place while only the card list scrolls (list text no longer slides behind the tabs), the tabs are equal-width segments with an animated active pill and live per-tab counts, section titles carry counts, and empty states show a centered glyph mark instead of a bare label. The Events timeline now also merges the chat's deleted and edited messages as red "Deleted" and blue "Edited" chip cards, so everything recorded about a chat is in one place.

### Fixed

- Update checks failed with "failed to check for updates" on every build since 1.0.12: the app asks GitHub for the update feed as `current6` (upstream moved the feed version from 4 to 6 in lib_base), but releases only ever published `current4`, so every check hit a 404. The feed now ships under both names and the live feed is already fixed, so the updater offers 1.0.14 on its own again.
- Mouse clicks in menus were still half-dead after the 1.0.14 fix on systems whose pointer stack tags real mouse input with the "synthesized from touch" signature: the first click reached a menu item (a submenu opened), but the same marking made every event ride the touch device, and after that first click hover stopped highlighting items and further clicks did nothing. The Windows Qt build now carries a patch (applied at build time) that only treats a mouse message as a synthesized touch duplicate while an actual touch is in flight — a real mouse is handled as a real mouse everywhere.

## 1.0.14

### Changed

- The Watcher box is redesigned as a card list: each session renders as a rounded card with a status dot (green = closed, pulsing = online right now, "?" = unknown start), Start/End timestamps and a duration chip with a clock icon; each event as a card with a kind chip (gift / name / username / photo) and its date. Sorting is now a one-click animated segmented control, and the toolbar gains a refresh button (re-reads the data, spins while at it) and a gear button that opens the tracking toggles without leaving the box. Cards animate in with a slight rise.

### Fixed

- Popup menus were dead to the mouse on some Windows pointer stacks (the "…", mute and "View Deleted" menus: items highlighted under the cursor, keyboard arrows + Enter worked, clicks did nothing and the menu would only close without acting). The application-level event filter was still dropping mouse presses that Windows marked "synthesized": it no longer applies while a popup menu is open (menus own their mouse input and already guard the touch cases themselves), and it no longer stays armed after the last touch sequence ends. The 1.0.12/1.0.13 fixes removed guards *below* this filter, which is why they changed nothing.

## 1.0.13

### Added

- New "Menu Corner Radius" setting (Appearance, 2-18 px) with a live miniature-menu preview that reshapes as you drag. No restart: every popup menu created afterwards follows the new radius.

### Changed

- Popup menu items now cross-fade their highlight (150 ms) instead of snapping, menu and box corners are rounder (10 px), hover highlights respond in 150 ms instead of 400, ripples are faster (300/160 ms), toast notifications fade quicker, submenu hover-aim opens 80 ms sooner, and section sliders (Watcher tabs, settings) glide with an ease-out curve.

### Fixed

- Popup menus were dead to the mouse on some Windows pointer stacks: presses marked "synthesized" were dropped by an upstream guard while hover and keyboard kept working. The guard is gone; the touch-press-and-hold case it protected stays guarded in the item release path.
- A sub-threshold drag from one menu item to its neighbour could trigger the first item instead of the one under the release.

## 1.0.12

### Added

- Synced with upstream Telegram Desktop 7.2.9 (HEIF decoding through FFmpeg, round video in topic shared media, clipboard crash fix, Windows logon fix, and more).
- Online History is now **Watcher**: same menu item, renamed, with two tab rows — All / Sessions / Events, and Newest / Oldest / Longest sorting. Longest applies to sessions; events fall back to Newest.
- Watcher now speaks Russian: the box, menu items and the Watched-chats rows, including gift and profile-change event texts.

### Fixed

- Popup menu items were dead to the mouse: a click landing without a prior hover-move over the menu was dropped by an anti-jitter guard (keyboard arrows + Enter still worked). Press now always registers; only a release with no matching press — e.g. the touch tap that opened the menu — stays guarded.
- Watcher's Events section showed the oldest rows first and counted the newest into "+N earlier" — the loader returns newest-first and the box read it backwards. Rows now render newest-first.
- Russian message-filter counts ("N фильтров") rendered an empty string for most numbers: the Russian plural needs #few/#many forms, which the override table never defined. Added.
- A Russian tooltip showed a literal "\n" instead of a line break; it is now one sentence.
- Unlimited favorites merge: the server list is now taken as-is and local extras are appended after it, so an extra that later shows up in the server list lands in server position instead of keeping a stale duplicate slot in the tail.
- Russian Watcher strings cover the "+N earlier" line, including plural forms.

### Known ceilings

- Favorites reordered beyond the server cap keep their order on this device; the capped part of the list follows the server order after each sync. An extra favorite unfaved on another device stays until unfaved here — locally it is indistinguishable from a beyond-cap favorite.

## 1.0.11

### Added

- Favorite stickers can also be reordered with a middle-click, which moves the clicked sticker straight to the front. The Chats sticker settings now say that this exists.

### Fixed

- Unlimited favorite stickers no longer shrink back to 5 on restart or on the hourly sync: extra favorites and their order are merged over the server list instead of being replaced by it. Extras live on this device.

## 1.0.10

### Added

- Favorite stickers can now be reordered: right-clicking one in the Favorites row offers "Move to Front".
- Luxury Extra gains an "Unlimited Favorite Stickers" switch that lifts the 5-sticker cap (10 with Premium) on favorites. The "Unlimited Recent Stickers" switch moved there from Chats, so both sticker limits live in one place.

### Fixed

- The Links rows in LuxuryGram settings are named after where they lead (Repository, Issues, Contributing) instead of Channel, Chats and Translate.
- Russian settings translations now cover the message-filter rows, the watched-chats menu, the sticker-limit switches and the Links rows, which previously fell back to English.

## 1.0.9

### Added

- New "No Selection Limit" switch in Luxury Extra: lifts the 100-message selection cap in chats. Off by default; attaching still caps at 100.
- Settings are now translated to Russian: with the Russian language on, every LuxuryGram settings row reads Russian instead of falling back to English.

## 1.0.8

### Fixed

- Exact last-seen times now tick every second while shown instead of freezing until the next minute.

## 1.0.7

### Added

- Gathered "Show Last Seen Seconds", "Track Online History" and the two "When Locked" companions into a new "Luxury Extra" settings section. The toggles themselves are unchanged.
- Exact last-seen times now name how long ago that was, e.g. "last seen today at 14:35:02 (1 ч 10 мин 10 с ago)".

## 1.0.6

### Fixed

- Translation and filter import failures now say what happened: a failed message in a batch translation is named (with how many siblings translated) instead of failing silently, unsupported filter backups are reported as unsupported instead of "no changes", and overlong or uncompilable filter patterns are named and skipped.

### Added

- Approximate statuses ("recently", "within a week") now show the tracked exact moment when both "Show Last Seen Seconds" and "Track Online History" are on and the offline transition was recorded; without a recorded moment they read as before.
- The "Online History" view now reads as sessions: every stay online is one row with its start, end and duration, a stay that is still open reads "since …", and one whose start was never recorded says "unknown start" instead of inventing a time. Only the newest 50 sessions are shown with a "+N earlier" line for the rest, and no totals are shown over that partial view.
- The "Online History" view gains an "Events" section under the sessions: gifts the contact gave or received (with the gift label) and their display-name, username and profile-photo changes, newest first, with the "+N earlier" line shared across both sections. Only the newest 30 events are shown, and nothing is totaled over the partial view.

## 1.0.5

### Added

- Added an "Anonymous" option to the message shot. Every name in the shot — the sender's, the one in a reply header, the one in a forwarded header — is replaced by "User 1", "User 2" and so on, numbered in the order they appear, so the same person keeps the same pseudonym across the whole shot. The avatar beside the message, the avatar inlined into a forwarded header, an emoji status, and a signature or custom rank are left out as well, since each of those names a sender just as plainly. Off by default. Service messages ("N pinned a message") are the one thing it cannot cover: their text is written once when the message arrives.
- Added an exact last-seen time. With "Show Last Seen Seconds" on, a contact's status reads as the clock time with seconds ("last seen today at 14:35:02") instead of rounding to minutes and hours. Approximate statuses ("recently", "within a week") stay as they are — the server never sends an exact time for those. Off by default.
- Added online history. With "Track Online History" on, every time a contact comes online or goes offline is recorded with the exact time, and the chat menu gains an "Online History" view with a clear option. Only genuine server presence updates are logged — the client's own +30-second guess after an incoming message is not. The newest 200 events per contact are kept. Both this and saving deleted messages keep working behind the local passcode, each with its own "When Locked" switch to opt out.

### Changed

- Moved the Telegram Desktop base from 7.1.1 to 7.1.5, picking up the upstream fixes in between (popup menus, screen handling, GIF status painting). No LuxuryGram feature was removed to make room; where upstream rebuilt something the fork had patched, the patch is gone and the upstream version stands in.

### Fixed

- Fixed the selected-message counter standing still while you drag. It only ever counted the messages already committed to the selection, and a drag reaches that selection when the mouse button comes up — so dragging over fifty messages read "1 selected" the whole way and snapped to the real number on release. Both the main chat and the pinned, scheduled and saved-message views were affected.
- Fixed a long selection dropping the messages you selected first. There is a hard limit of 100 selected messages, and the limit was always applied from the oldest end of the range, whichever way the drag went — so selecting upwards from a recent message kept the far end and quietly threw away everything near where you started. Separately, dragging past the limit, or across an album with a part hidden by a filter, took messages that were already selected back out of the selection instead of leaving them alone.
- Fixed being unable to write in a chat where the message you were replying to had been deleted. LuxuryGram keeps a deleted message on screen, so the reply stayed in the compose field and every send failed with `MESSAGE_ID_INVALID` — including a chat opened straight from a profile, where nothing on screen explained what the client was still holding on to. A reply the server cannot resolve is now dropped from the two sends that were missing that step (a location or venue, and a music selection), and any send refused for that reason now treats its target as the deletion it is — so the message is marked deleted, as it would be had the delete reached us, and the next attempt goes through with the reply turned into a quote.

- Fixed the update button leaving every installation on the version it already had. Downloading and unpacking worked; the last step, which decides whether a prepared update may be installed, compared the package against the Telegram Desktop version this fork is built on (7.1.1) instead of the LuxuryGram release counter the packages are numbered by — so `1000004 <= 7001001` held, and the ready update was deleted on every launch with nothing but a line in the log. Two of the three version gates were converted when the fork took its own counter and the third was missed, which is why no release since 1.0.1 has been able to install itself. That check lives in the installed application, so 1.0.1 through 1.0.4 cannot pick this up: install this version by hand once, and self-updates work from there on.
- Fixed ghost mode discarding a message sent as an ephemeral bot command. Ghost mode sends as a scheduled message so no read receipt goes out, and a scheduled ephemeral send is refused outright — by which point the compose field has already been cleared, so the message simply disappeared. The compose widgets say no to that combination to your face; ghost mode no longer routes around them.
- Fixed ghost mode taking the self-destruct off a photo, sticker, file or voice message answering an ephemeral bot: the flag that makes such a message disappear is only set when the send is not scheduled, so the message quietly stayed in the conversation as an ordinary one. Files sent through the attachment box are not covered — that box does not know which chat it is sending to.
- Fixed "Send without Sound" playing a sound. With the setting on, choosing "Send without sound" from the send menu flipped it back to an audible send, because the setting returned the opposite of the menu choice rather than adding to it. Nothing in the menu asks for a sound back, so the setting now only ever silences.
- Fixed the monospace font box throwing away your font. Pressing OK without picking a row passed an empty font to the setting, which is exactly what that box's Reset button does — so opening the box to look and confirming out of it reset the font and offered a restart for the privilege.
- Fixed the message-shot theme box switching you to the light theme. Apply without choosing a theme handed over an untouched palette, and an untouched palette materialises as the built-in light one, so it arrived looking like a deliberate choice. Apply now does nothing when nothing was chosen.
- Fixed the deleted- and edited-mark box accepting an empty mark from its Save button while refusing the same thing from the Enter key, and storing whatever spaces were left around the mark.
- Fixed a plugin's author being printed twice in the plugin information box whenever the author was not written as a Telegram username.
- Fixed the "Wide Messages Multiplier" slider doing nothing at all. Its value crossed into the interface layer and stopped there — nothing had ever read it — and the comparison deciding whether the multiplier differs from 1.00 truncated to a whole number, so every setting below 2.00 read as 1.00 even once something did. It now raises the limit on how wide a long text message may grow, which is what its description promises.

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

