# Changelog

This file tracks changes specific to LuxuryGram. Historical Telegram Desktop and inherited AyuGram changes remain available in [`changelog.txt`](changelog.txt) and the Git history.

Releases are published on the [Releases page](https://github.com/GofMan5/LuxuryGram/releases) and tagged `luxury-v<version>`.

## 1.0.22

### Added

- JavaScript plugins. Settings > LuxuryGram > Plugins > Install from GitHub takes a link such as `owner/repo`, `owner/repo@v1.2.0` or a repository URL, downloads the plugin from the latest GitHub release (or the default branch when there are no releases) and shows what it asks for before anything is installed: every permission with a description, the hosts it may reach, and a warning on the ones that can send messages, call the Telegram API or read the update stream. A plugin runs in its own sandboxed QuickJS engine with no file or process access, only through the `lux` API its permissions open: reading incoming messages, sending and rewriting or cancelling outgoing ones, its own `/commands`, message menu items, hotkeys, toasts and dialogs, HTTPS requests to its declared hosts, its own storage, the clipboard, raw Telegram API methods and the raw update stream. Methods that spend Stars or money, change the login, sessions, password or privacy, or grant a bot access to the account are never available to a plugin, whatever it was granted, and neither are the Telegram service chat with the login codes and the account's own phone number. A plugin that keeps the interface busy (over 500 ms of JavaScript in 5 s) is stopped with an error. The Plugins page lists installed plugins with their state, version and source, and lets you enable, disable, reload, update, remove them, change their settings and read their log. An update that asks for new permissions or hosts shows the consent box again. The plugin format and API are documented in `docs/plugins/ARCHITECTURE.md`.
- Plugin authors get `docs/plugins/AUTHORING.md` (manifest, settings, permissions, limits and errors in one place), `docs/plugins/lux.d.ts` typings for editor completion and a working `docs/plugins/examples/hello-world`. Plugin settings can be `string`, `bool`, `int`, `select`, `number` (with `min`, `max` and `step`), `color` (`#RRGGBB`) or `secret` (never logged and masked in the settings box). The settings box shows `description`, `placeholder` and the translations from `locales` in the client's language. Errors in `plugin.json` name the exact field, such as `settings[2].default: must be one of options`, and a bad id shows the value that was given.
- The message context menu, next to Forward and Forward Selected, has two submenus. Forward Options opens the chat picker already set to Hide Sender Name, or to Hide Caption when the message has a caption. Forward to Saved Messages forwards straight to Saved Messages, with no chat picker, as is, without the sender name, or without the caption, and shows the usual "Message forwarded to Saved Messages" toast without closing any window you open meanwhile. If Saved Messages cannot take the messages, an error box explains why and the selection is kept. Inside Saved Messages itself only Forward Options is offered. Both work for a single message, an album and a selection, and offer only the variants the forward box would offer for those messages. A Forward Variants row under Context Menu Elements in Settings can hide both submenus or move them to the extended menu.
- Clicking the cover art in the expanded music player opens it in the system's default image viewer. The pointer turns into a hand over a real cover; the "no cover" placeholder is not clickable.
- Space pauses and resumes the playing song or voice message in the main window whenever the keyboard focus is not in a text field: in the chat list, in channels and other chats without a message field, in profiles and in boxes. A typed space is never lost: in a chat with a message field, focus stays in that field and Space keeps typing. Space also keeps its usual job on controls focused with Tab and on a Settings row picked with the arrow keys. With nothing playing, in the media viewer, and in screen reader mode, Space works as before.

- Plugins survive a crash or a killed install. Plugins start one at a time, and a plugin that takes the client down while it starts is disabled at the next launch with a "closed unexpectedly" error; the plugins started before it stay enabled. An update interrupted halfway is finished or rolled back at the next start, and a damaged plugin list is restored from its backup. `-noplugins` on the command line, or Shift held while the client starts, starts it with every plugin stopped, and plugins now start only after the main window is shown.
- Plugin updates can be rolled back to the previous version (a rollback cut short by a crash keeps that version for the next try), and a plugin can be pinned to its installed version so update checks skip it. Installing an older release than the installed one has to be asked for explicitly. `lux.plugin` tells a plugin its previous version and why it was started.
- Plugin install links accept `/blob/...`, `/commit/<sha>`, `/releases/latest`, `git@github.com:owner/repo.git` and branch names with `/`. The same repository cannot be installed twice by changing the letter case. When a release has no `plugin.json`, the plugin is installed from the default branch and its log says so. Plugin log lines also go to the client log, one line each. Updating a plugin that is already at the latest version says so instead of installing it again.
- The Plugins page is easier to manage. Install, update and network errors read as plain sentences in your language instead of raw GitHub or Qt text. The consent box warns when two permissions together let a plugin send your messages, account data or clipboard to the internet, marks the websites an update adds, and an update shows the old and new version with an Allow and update button. An install in progress is listed with a Cancel button. Plugin rows update in place, long errors are cut to three lines and screen readers hear each plugin's state. Check for updates now also says why a plugin could not be checked, and the Update button appears only when an update exists. Reload and Remove say whether they worked. The details box follows the log live, filters it by level, copies the shown lines (up to 500), and lists the allowed websites and a link to the source. The Plugins entry in Settings shows how many plugins have an update.
- GitHub rate limits are reported with the time requests may resume, API answers are reused through ETag, and an update check reports how many plugins could not be checked instead of saying everything is up to date.
- Plugins get typed access to messages and chats. `lux.messages` can read a message, load chat history, edit, delete, forward, mark read, send a file of up to 10 MiB from bytes or the plugin's own folder and download a photo or file of up to 10 MiB (never in chats that restrict saving); `lux.chats` lists and describes chats, and `lux.account.me()` describes the account without its phone number. Editing, deleting, forwarding and marking read need the new dangerous `messages.manage` permission, which the consent box shows as "Manage messages". Plugins with `messages.read` also hear about edited and deleted messages, read chats and account switches, and messages now carry their formatting, media type and size, forward origin, topic, edit date and album. `beforeSend` sees and may change the formatting and the replied message, and an `async` handler is reported in the log instead of being silently ignored. Menu and hotkey handlers learn which chat they run in, `lux.settings.set` changes a setting and `lux.ui.notify` shows a toast titled with the plugin name. The Telegram service chat stays out of all of it. `docs/plugins/examples/auto-moderate` shows them together.
- Plugins can add items to a chat's menu (in the chat list and the three-dot menu of the chat header), a plugins button in the chat header and a Plugins submenu in the message field's context menu; each item gets the chat it was used in and disappears when the plugin is turned off. A plugin can show a form with text fields, switches and choice lists, learn which chat is open and when another one opens, open a chat, open a web link right after you use one of its items, through the usual link confirmation, and put a short label on an incoming message that shows its sender's name. Plugin `/commands` show up in the command list when you type `/`.
- WebSocket transport for networks that block direct connections to Telegram. Settings > LuxuryGram > Other > Network > WebSocket transport ("Транспорт WebSocket") offers Off, Automatic (the default) and Always. In Automatic mode the app tries the usual direct route and, at the same time or a couple of seconds later, an encrypted WebSocket connection to Telegram's own web servers (`kws*.web.telegram.org`, the ones Telegram Web uses), and the app remembers for each data center and its media servers which route worked. A data center reached over WebSocket is re-checked for a direct route from time to time, a WebSocket route that is refused or fails waits a couple of seconds behind the direct route for a while, a WebSocket connection that stalls lets the direct route race again, and a direct route that connects and then stalls is left out for a while. Always uses WebSocket only; Off keeps the direct connection only. WebSocket is not used while a proxy is set in the app, or while the system proxy setting is on and the system reports a proxy. The row under the switch shows the current status, and WebSocket log ("Журнал WebSocket") lists the last transport events, with buttons to copy it for a bug report or clear it; the same lines go to the client log. Keys, login data and message bytes are never logged. Details are in `docs/ws-transport.md`.

### Changed

- Synced with upstream Telegram Desktop dev fixes after 7.3.3. Text dragged onto a chat in the chat list is added to that chat's draft, and a shortcut in Settings > Chat Settings > Keyboard shortcuts can be reset or deleted from its context menu. Holding the speed button of the audio player bar above the chat plays at 2x while it is held, and dragging sideways while holding changes that speed. Ctrl+C keeps the message selection again; only Copy Selected Text in the message context menu clears it afterwards, and copying selected messages as text no longer shows a "copied" toast. The video editor turns a video into a GIF only up to 10 MB and explains the limit on the disabled button. Upstream also fixed poll answers overlapping the vote count, save hotkeys and lost keys in the media viewer, '?' in the image editor opening the app's shortcuts legend instead of the editor's, GIF conversion of videos other than MP4, videos without an audio track sent without the no-sound flag, and search keeping the focus after a chat is opened from its results.
- Plugins are held tighter. The clipboard permission is marked as dangerous in the consent box, and a plugin can use the clipboard only within a second after you run one of its commands, menu items or hotkeys. `lux.http.fetch` refuses hosts that are or resolve to local network, loopback and other private addresses, leaves a host name to the proxy when one is set in the app (only IP addresses are checked then), drops a plugin's Host header, and drops Authorization, Cookie and proxy headers on a redirect to another site; `{ binary: true }` returns the body as base64. Plugin dialogs carry the plugin name in the title, open one at a time, close when the plugin is turned off and are not shown while the app is locked with a passcode; toasts are limited to one a second. Switching or logging out of an account rejects the plugin's pending requests. A plugin flooding the app with tiny callbacks, timers (now at most 64, an interval at most every 50 ms) or events is stopped or has its oldest events dropped, unhandled promise rejections are logged at most 16 at a time, subscribing to an unknown event throws, and `lux.plugin` lists the granted permissions and the interface language. The Telegram service chat stays hidden however its id is written.
- Select messages between extends a selected range up to the full selection limit: select one more message above or below the range and choose Select messages between again, and everything from the range's nearest edge to that message is selected. Messages already selected inside the range used to count against the 100-message limit a second time, so a 40-message range plus a message 30 below it lost the action although the result is 70 messages; now the action is offered whenever the result fits in 100. Select up to this message also no longer refuses a downward range that lands exactly on the limit; it was one message short going down but not going up. This works in chats and in replies, scheduled messages and other message lists.
- Background work is sized to the processor instead of fixed numbers. GIFs and video previews in chats play on 8 to 16 decoding threads (always 8 before), animated stickers and emoji render on 1 to 8 queues (a single one before), and file downloads and uploads use 1 to 4 connection threads in each direction (half of all logical CPUs before, which on a 64-thread processor meant 32 idle threads each way). The counts follow the CPUs the app is allowed to use (on Windows a process pinned to efficiency cores counts only those, at half weight), count efficiency cores of hybrid Intel and Apple processors as half a core, and always leave one CPU free for the interface. On Windows machines with fewer than 4 logical CPUs, background jobs get at least 4 workers, so one job waiting on another no longer holds up the rest.
- More decoding and waiting moved off the interface thread and off shared background workers. Videos played in the media viewer decode on every worker thread the budget above allows, as before, and VP9 stickers and other decoders are given an explicit thread count instead of guessing; a thread count set in the FFmpeg options still wins. The first frame of a GIF or video without a server thumbnail, cached custom emoji and animated sticker files are now decoded off the interface thread. Forwarding with the intelligent forward, repeating rich messages and fetching player covers wait on their own threads, so they no longer hold background workers that media decoding needs.

### Fixed

- A voice or round video message paused in the record bar survives the automatic app lock: after unlocking, the chat is still open with the draft in the bar, ready to play, resume or send. The lock screen used to close the open chat once it slid in (and again when the locked window was closed to the tray or taskbar), and closing the chat threw the draft away. A recording still running when the app locks is paused into a draft the same way without pulling the window to the front, and Enter or Space typed into the passcode field no longer sends or plays the draft behind the lock. This replaces the one-second post-unlock guard from 1.0.20, which never caught the real cause.

## 1.0.21

### Changed

- Synced with upstream Telegram Desktop 7.3.3, up from 7.2.9: Money (the TON wallet card, with unique gifts moved to the blockchain and shown on the card), replies quoting rich messages, the new QR code design for public usernames, emoji from Unicode 17, Ctrl+Enter to submit the rich message editor, audio tracks of a rich message played as a playlist, screen reader improvements, and upstream fixes for text rendering that ignored system settings on Windows, Chinese characters on older Windows, the ANGLE media viewer and video tint, Windows Focus Mode, '?' opening the shortcuts legend instead of typing, and a set of crashes. LuxuryGram features stay where they were; the Wallet item joins the side menu next to them.
- Windows builds compile the Rust components (wallet engine) that upstream added in 7.3, and the Windows CI builds Qt 6 on every architecture, as upstream does: Telegram Desktop 7.3 no longer compiles with Qt 5 on Windows. The generated language key lookup is split into smaller functions, since the Windows ARM64 compiler refused it as one function once LuxuryGram's keys were added to 7.3.3's.

### Fixed

- Dragging photos and files from Explorer into a chat works again after an update installed into a folder that needs administrator rights. The updater meant to restart the client as a normal user, but saved the restart shortcut into a folder it had just removed, so the client came back with administrator rights, and Windows drops every file dragged into an administrator window from a normal Explorer. A client already running with administrator rights needs one normal restart.
- The experimental "Use small message bubble radius" option (Settings > Advanced > Experimental) squares off message bubbles and file thumbnails again; the bubble rounding slider ignored it.
- The Clear Watcher Data confirmation now has the same layout as the Clear Deleted Messages box next to it. Its question used to sit almost against the top edge of the box, and the "Also delete saved media files" checkbox was pressed against the last line of the question. In Russian the box also lost its Cancel button past the left edge and cut the checkbox to "Также удалить сохранённые фай…": the confirm button now says "Clear" ("Очистить") and the checkbox wraps to a second line.
- Settings > LuxuryGram > Other > Reset Settings now confirms with a red "Reset" ("Сбросить") button instead of a blue "Yes", so the irreversible reset of every LuxuryGram preference no longer looks as harmless as Cancel next to it.
- Filters import from a link (Settings > LuxuryGram > Filters > Import > Import from URL) now starts on Enter in the link field instead of only on a click of Import, and when the clipboard already holds a link the box opens with the cursor in that field.
- Saved music on a profile now follows a day/night switch while the profile is open. The song row kept its day colors: in night mode its title and performer stayed dark on the dark row (contrast 1.06:1, unreadable; 14.52:1 now) and the cover without artwork stayed light until the profile was reopened. The cover placeholder in the expanded player also keeps up with the switch instead of staying in the old theme while the player is open.
- The saved music cover for a song without artwork is sharp on high-DPI screens. It was drawn at 100% size and stretched, so at 200% the disc icon was smeared: a 38 px picture blown up to 76 screen pixels. It is now drawn at the screen's own resolution, as in the expanded player.
- The "edited" mark next to the time of an edited message (shown when Settings > LuxuryGram > Chats > Replace with Icons is off) now follows the interface language. It was taken before the language pack loaded, so it was always English "edited"; the first change of any LuxuryGram setting saved that word, and from then on messages and Settings > LuxuryGram > Chats > Edited Mark said "edited" in every language. Settings saved by earlier builds with that English default switch to the language's own word on the next start; a mark you typed yourself stays as it is. Reset in the Edited Mark box now keeps following the language instead of fixing the word of the language active at that moment.
- The plugin details box (opened from a `.plugin` file in a chat) shows the dependencies of plugins that list them as `__requirements__ = ["mpmath", "tinydb"]`, the form the exteraGram plugin documentation uses. Such plugins showed no dependencies at all; 5 of the 16 public plugin files on GitHub that declare dependencies use this form. A dependency pinned to a version, such as `kangelpluginsmanager==1.4.4`, now opens its project page on PyPI instead of a page for a project named with the version, which does not exist. An entry without a package name, such as `>=2`, no longer appears as a dependency.
- Dates in Message Details (sent, edited and forwarded) now follow the date and time template of the interface language, word order included. The connecting word between date and time was taken once, at the first use, and kept for the whole run, so after a language change it stayed in the old language; and for languages that do not put a word between date and time it was glued to the time, e.g. Uzbek showed "25.10.26  da15:30:44" instead of "25.10.26 15:30:44 da", and a template with the time first still showed the date first.
- Watcher's Events list shows the most recent deleted messages of a chat. It read the oldest ones first, so in a chat with a long deletion history the newest deletions fell outside the loaded window. When either the deleted or the edited history is longer than the window, the timeline is now cut at the same point for both, and the rest is counted under earlier events instead of showing only one kind of event at the bottom.
- The pulse on an active Watcher session no longer runs when animations are turned off in Telegram settings.
- Watcher and online history open faster on large databases: the tables they read and prune for each chat are indexed by user, chat and time instead of being scanned whole.
- Deleting a filter removes it together with its exceptions in one database transaction, so a crash in between can no longer leave orphaned exceptions behind.
- Turning filters on no longer freezes the chat for a moment on the first painted message: the filter cache is now built in the background at startup.
- Importing filters with several invalid patterns shows one notice with the first problem and how many more patterns were skipped, instead of stacking one notice per pattern.
- The message screenshot theme picker no longer stays on the loading state when a theme file fails to download, and themes made only of emoji apply right away.
- Reaction updates no longer resolve custom reaction documents when Watcher is off.
- Selecting messages in profile media and file lists no longer rebuilds the screen reader item list on every change when no screen reader is running.
- Streamer mode hides LuxuryGram from screen capture on Windows 10 versions before 2004 too: they reject the newer capture exclusion, and the window used to stay visible to recording.
- A Message Width value below 1.0 in a hand-edited settings file is reset to the default instead of being kept and doing nothing.
- Right-clicking the author name in the deleted and edited message history opens the usual message menu instead of an empty one.
- Opening a story from the "open with or without ghost mode" prompt no longer crashes when the story expired or was deleted while the prompt was open, or when its account was logged out.
- Voting in a poll, pressing an inline bot button or reacting with ghost mode's "mark read after action" on no longer crashes when the update that came back deleted the message.
- Archiving a chat with archive folders switches the chat list folder only in that account's own window; another account's open window used to jump to an unrelated folder.
- A failed forward of a message whose source reply was deleted no longer deletes a reply target in your chat; only a reply you chose yourself is treated that way.
- The topic ID row in a forum topic profile no longer crashes when the topic was deleted while the profile was open.
- The blocked users list is loaded only when hiding messages from blocked users is on. It was walked at every start, and each blocked user re-laid out the open chats.
- Changes to the blocked users list re-lay out the open chat only when hiding messages from blocked users is on, and a burst of changes (such as the list loading) re-lays it out once instead of once per user. When the message field hides the emoji button, the field no longer runs under the bot keyboard hide button.
- Save dialogs open at once when the last used folder is on a network share that went offline; after the first 2 s wait the share is no longer probed again while it stays unreachable.
- The ripple on a reposted story's source bar is drawn in the bar's color instead of black.
- The pinned taskbar shortcut icon is rewritten only when the icon actually changes, not on every unread counter update; a failed write is retried.
- Local Premium applies only to your own account. With several accounts logged in, it made every other user in the account switcher look Premium.
- "Send as sticker" in the send files box shows "Could not attach" when the image cannot be converted, instead of sending an empty sticker.
- Searching join requests waits 1 s after typing before asking the server, as upstream Telegram does, instead of querying on almost every key.
- A phone number link that the server rejects shows nothing instead of opening the profile of an unrelated user.
- The "GitHub Releases" button in the About box follows the interface language.

## 1.0.20

### Added

- Startup loading window. A launch used to show nothing until the main window was ready, about 2–3 s on a test Debug build. A small card with the logo, version and current step now appears about 1 s after the launch and fills a progress bar until the main window opens. It has no taskbar button and does not take focus, and it is skipped when LuxuryGram starts to the tray or minimized at system startup. Streamer mode hides it from screen capture.
- Arch Linux package recipe `luxurygram-bin` (`packaging/aur/luxurygram-bin`). It installs the official Linux x64 release build as `/usr/bin/LuxuryGram` (also `luxurygram`), with the menu entry, icons and `tg://` link handling, and checks every download against a pinned SHA-256. Updates come through pacman, so the built-in updater is off in that install and its update button opens the Releases page. Once the recipe is published on the AUR, it installs with `yay -S luxurygram-bin`.
- The Repeat message context-menu action now re-sends messages that contain a rich page (for example a web page preview) as the full rich page for premium users, instead of replacing them with a plain-text copy.
- Forwarding a selection that includes messages with rich pages now re-sends those messages as the full rich page for premium users instead of degrading them to a plain-text copy.
- Clear Watcher Data in the chat context menu now opens a confirmation box that also offers deleting the media files Watcher saved for that chat, instead of only clearing recorded online history.
- Watched chats now report media that could not be saved: when Watcher gives up retrying a file, a toast says how many files in the last minute failed, instead of the loss being visible only in the log.
- The context menu over a message selection gained a Select messages between action: with two or more messages selected, it selects every selectable message between the outermost selected ones in one step, respecting the selection limit, instead of requiring each message to be selected by hand.
- The media player gained an expanded view: a new button next to the player close button opens a box with the track's cover (embedded or fetched from the iTunes catalog), title, performer, a seekable progress bar with position and duration, and full playback controls, plus a button that saves the current cover as a PNG file.
- URL buttons in rich pages (web page previews) now show the destination URL in a tooltip and offer Copy Link, instead of giving no hint of where the button leads.

### Changed

- Keyboard-focused buttons, folder tabs, chat composer controls, audio playback controls and settings rows show a theme-aware focus outline, making navigation visible without changing the familiar chat layout.
- Secondary text in the default light palette is darker and easier to read; custom themes retain their own colors.
- Settings with descriptions use a tighter, aligned two-line layout and a visible hover state. The LuxuryGram preferences overview has a localized page title and no longer inserts four spacer rows before its categories.
- Settings > LuxuryGram > Appearance: "Disable Custom Backgrounds" is now "Hide Chat-Specific Wallpapers" ("Скрыть обои отдельных чатов" in Russian). The switch only ever hid a wallpaper set for one chat or channel, never your own chat wallpaper; the old name made a chat with its own wallpaper look broken. Settings search still finds it by "background" and "custom".
- Watcher identifies the selected person, explains the limits of observed history and shows when recording is paused. Its settings use native toggles and explicitly identify recording as an all-chats setting; saved history remains accessible when recording is off.
- Watcher reads saved history off the UI thread and refreshes idle views every five seconds, preserving scroll position and expanded event details. Automatic refresh pauses only while a row is held pressed and catches up the moment the press ends; active-session duration updates every second and timestamps include seconds.
- Plugin descriptions in the plugin details box now render bold text, links and @mentions instead of showing raw markdown-style markup.
- The Watcher box header now notes when its view updates are held for a mouse press, so the paused list is explained instead of looking stuck.
- On Linux, bot web apps now use the external desktop shell only on Wayland sessions; on X11 they open as regular in-app panels again.
- The shadow-ban and watched-chats lists act directly on row activation: the ban is lifted or watching stopped, the row disappears, and a toast confirms what changed — instead of opening a cursor-anchored one-item popup with a generic "Delete" label that keyboard activation could not aim at the row.
- The Watcher events section now discloses how much older recorded history exists beyond the loaded window, as a measured "+N earlier" count taken from the database, and Show more extends only the list that actually overflowed instead of deepening both windows.
- The lock-time switches in Settings (Save Deleted When Locked, Track Online When Locked) moved out of the Luxury Extra block into Spy essentials, next to the recording switches they guard, and each now describes exactly what keeps happening while the app is locked. Track Online History, Track Online When Locked and Show Last Seen Seconds moved there too; Luxury Extra keeps only the limits that have nothing to do with recording.
- The watched chats block in Settings now appears from the start: empty, it explains that chats are added with the Watch Media action in the LuxuryGram chat menu, and once chats are watched it notes that saving continues while the app is locked. The block is also reachable from settings search by "watch" and "media".
- The Watcher box gear menu gained a Watcher header row, a note that online history keeps recording while the app is locked, and an Open Luxury Settings action that jumps to LuxuryGram Preferences in Settings and brings the window forward.
- The LuxuryGram description in Settings now reads "customization and privacy features" instead of "ToS-breaking features".
- Rich pages (web page previews) now match ordinary message bubbles: edge media is rounded like bubble media, plain text keeps the same paddings at the bubble edges, and article width and the last line fit the bubble instead of spilling past it.
- On Linux, mini apps now ask for camera and location permission before use, matching the behavior on other platforms.
- Web app loading progress now appears when the main frame starts navigating instead of flickering on sub-frame activity.
- WebRTC and ANGLE dependencies were refreshed: stopped transceivers are cleaned up, VP8 temporal bounds are fixed, and a Windows vertex value cache fix lands with ANGLE.

### Fixed

- Plugin info window: a plugin name, author, description, version or requirement that runs past its limit (100, 200, 8000, 32 and 128 characters) no longer ends in half an emoji. The same goes for the monospace font family in Settings (256) and the message screenshot cloud theme name (512). Before, the cut left a broken character that drew as a box and was saved to the settings as "?".
- Pasting or typing past a text field's length limit no longer leaves half an emoji at the end, and the deleted and edited marks keep the last emoji whole when they are cut to 64 characters. Before, the field kept a broken character and the mark drew a box after the message time.
- Settings > Chats > Message Field Elements calls the auto-delete timer button "Auto-Delete" ("Автоудаление" in Russian), the name Telegram uses for it, instead of the abbreviation "TTL".
- Message menu > Details > Datacenter now names the city in the interface language ("DC2, Амстердам, Нидерланды" in Russian) and spells countries out in English ("DC2, Amsterdam, Netherlands" instead of "DC2, Amsterdam, NL").
- Message menu > Details > Views and Shares now group digits the way the views tooltip on the same message does: "1 234 567" (or "1,234,567", depending on the system locale) instead of "1234567".
- A reply to a deleted message no longer sends a "?" in place of an emoji. The reply is sent as a quote of the first 100 characters, and when the cut fell inside an emoji, the chat received "…aa?…" instead of it. The cut now drops the split emoji whole, and a quote of exactly 100 characters is sent in full instead of losing its last character. The same split emoji no longer shows up as a box at the start of a long file name in Message menu > Details, or at the end of a long filter pattern in the filter import toast.
- Message menu > Details > "MIME Type" is no longer blank for FLAC audio, RAR archives and other types Qt does not know: it shows the type the sender attached. Measured on 38 common types: 7 came back empty, among them `audio/flac` and `application/vnd.rar`. The English label now reads "MIME Type" instead of "Mime Type".
- Message menu > Details > "File Size" now shows the real size of files over 2 GB. Before, it printed a negative byte count: a 2.5 GB video read "-1610612736 B".
- Settings > LuxuryGram > General > "Filter Zalgo" no longer strips the vowel and tone marks of Burmese, Tibetan, pointed Hebrew and Devanagari. Before, any letter carrying three marks lost them all: Burmese "လို့" showed as a bare "လ", and the Burmese Wikipedia article on Myanmar lost marks in 271 places. The filter still removes stacked accents, one mark repeated, more than five marks on a letter and hidden text-direction controls.
- Cancelling the "Send files" window no longer empties the message field in two places: a story reply with a photo attached, where the reply you had typed becomes the caption, and a long pasted text sent as a `.txt` file after you typed a caption for it. Before, Cancel, Esc or removing the last file left the field empty; now the reply, or the text you had before the paste, comes back. Attaching files in a chat already returned the caption with your edits and still does.
- "Save As" no longer freezes the app when the last folder you saved to is on a network share that is offline or unreachable. Before, the window stopped responding for 41 seconds just while picking a file name, before the dialog even started to open. Now LuxuryGram waits at most 2 seconds for the share and then opens the dialog in Downloads, which becomes the remembered folder: 2.2 s from the click to the dialog on Windows with an unreachable share.
- Windows: an icon you set yourself on a LuxuryGram taskbar pin or Start menu shortcut (Properties > Change Icon) now stays after a restart. Before, the first start after an update, or after the app icon file in `tdata` was rewritten, put the LuxuryGram app icon back on every shortcut. Shortcuts with no icon of their own, or with the icon LuxuryGram or AyuGram set, still follow Settings > LuxuryGram > Appearance > App Icon.
- "Download Selected" in a chat's selection menu now saves small media that Telegram keeps in its media cache — GIF-like videos without sound, round videos, voice messages, stickers — to the download folder like any other file. Before, those messages were silently skipped while "Save As" on the same message worked.
- Opening a `.plugin` file no longer leaves you waiting with nothing on screen when its sticker icon fails to download: the plugin info box opens without the icon. In the message screenshot theme picker, a theme file that fails to download now shows "Could not load the theme file" instead of keeping the choice pending.
- Custom forwarding moves on as soon as a photo or file fails to download and skips that message. Before, a failed photo held the whole forward for its full 5-minute timeout, and a failed file waited until some other download in the app happened to finish, for up to 15 minutes. A photo that had already failed to load in the chat is now downloaded again for the forward instead of being skipped after the same 5-minute wait.
- Search in a chat’s saved deleted messages now ignores letter case for every alphabet, so “привет” finds “Привет, мир”. Before, it ignored case only for Latin letters, and a Cyrillic or other non-Latin query had to match the case of the message exactly.
- The per-chat lists in LuxuryGram settings (chats with their own message filters, shadow-banned chats and watched chats) and the title of a chat’s filter page now show the right group or channel. Before, a group or channel whose number matched a loaded user was shown with that user’s name and photo.
- When a custom forward is sent in several parts, the progress bar above the message field shows “skipped N” only for the part that actually dropped messages. Before, the count from an earlier part stayed on screen for every later part, even when nothing was skipped there.
- A LuxuryGram `chat?id=` link now opens only a group or channel with that ID. Before, if a user with the same number was already loaded, the link opened that person’s chat instead, and when nothing was found it said “User not found”. It now says “Chat not found”.
- Times shown with seconds now follow the system 12/24-hour clock: Message Details (sent, edited, forwarded), the exact last-seen status, and the watcher timeline. Before, they always used the 24-hour format, so with a 12-hour system clock they read 21:05:09 next to messages showing 9:05 PM. The seconds are still shown.
- In a separate chat window, the LuxuryGram menu items History (edits of a message), User Messages and View Deleted now open in that window. Before, they always opened in the main window: the edit history and the deleted messages replaced the chat shown there, and User Messages started its search in the main window’s chat list.
- The “Filter added” notice with its “Only this chat” button, and the “could not save to the database” error from the filter editor and the filters list, now appear in the window you edited the filter in. Before, they always went to the main window, so a filter added from a message in a separate chat window was confirmed out of sight. The same applies to the “could not translate” notice when translating a whole chat: it now appears in that chat’s own window when the chat is open in one.
- Copy actions added by LuxuryGram now confirm the copy with a toast, as the rest of the app does. This covers “Copy ID” on your profile in Settings and on an emoji in a custom emoji set, and the Message Details rows (ID, dates, views, file size, type, name, resolution, datacenter). Before, they copied silently. The “ID copied” toast of the sticker pack owner row, shown when the owner is not found, now appears in the window where the menu was opened instead of the main one.
- Message Shot, its theme picker, the “user not found” notice for `luxurygram://` user and chat links, and the drawer’s “Read all messages” confirmation now open in the window they were called from and on top of what is already open there. Before, they went to the last active main window, so a shot taken in a separate chat window appeared in the main window instead. All but the theme picker also closed any box already open in that window. The “shot too large”, “could not save the shot”, “could not load the theme preview” and “could not save the cover” (expanded player) messages now also appear in that window instead of the main one.
- Popup and context menus keep their rounded corners in proportion at interface scales above 100 %. LuxuryGram → Appearance → Menu Corner Radius used its 2–18 px value unscaled, so at 150 % every menu had a 10 px corner instead of the 15 px the scaled default gives, and 18 px instead of 27 px at the maximum. The value now scales with the interface, both in menus and in the settings preview.
- On profile gift cards with the sender’s avatar, the pin and TON badges no longer overlap that avatar when LuxuryGram → Appearance → Avatar Corners is set below Circle. With square avatars the pin used to cover 4 px of the avatar and sit 6 px higher (100 % scale); now it sits 2 px to the right of the avatar, level with it, at every corner value and scale.
- The “Text copied to clipboard.” toast after clicking the phone number in Settings → Info stays for 1.5 s, like other copy toasts, instead of disappearing after 0.5 s, most of which went to fading in and out.
- The “Could not translate: …” toast after a failed chat translation no longer shows the hidden text of spoilers in the failed message; spoilers appear as ▚ blocks, as in notifications. A cut no longer splits an emoji in half, and the toast stays on screen for 4 s instead of 1.5 s, long enough to read the message excerpt and the count of messages that did translate.
- LuxuryGram → Other starts at the same height as the other LuxuryGram settings pages. The “Other” heading used to sit an extra 6 px lower (12 px at 200 % scale), in the space left over from the removed donations block.
- The watcher window no longer titles its sections “Sessions · 0” and “Events · 0” while the block underneath still says the saved history is loading; the count appears once it has been read.
- Russian LuxuryGram settings keep their own wording after AyuGram's community translation downloads. Before, it replaced 117 of the 192 strings both sources cover, so “Режим призрака” stood next to “гост-режим” and labels changed between launches. Strings from that translation in any language now say LuxuryGram instead of AyuGram (reset confirmation, “Plugins are not supported…”, URL scheme). The English interface no longer downloads it at all; it used to replace 10 English strings, describing the app as having “ToS-breaking features”.
- LuxuryGram → Appearance → Menu Corner Radius: the menu preview shows its corners again. It was painted in the colour of the settings page, so moving the slider from 2 to 18 px changed nothing visible. The preview now sits on a grey stage with the shadow of a real menu. In the night themes, the hovered row lights up in the menu hover colour instead of white, and pressing a row shows the same ripple as a real menu.
- In LuxuryGram → Appearance, “Single Corner Radius” and its note about forum avatars sit under the avatar corners slider again instead of under “Menu Corner Radius”, which they have nothing to do with; the menu radius block ends with its own separator.
- The expanded music player shows the elapsed and total time as soon as it opens, including for a paused track; the total time used to stay hidden off the right edge until playback produced the next update.
- The expanded music player's progress bar is visible before playback starts and between seeks. It used the mini player's bar, which only paints the played part until hovered, so at 0:00 the box showed an empty gap between the cover and the controls with nothing to drag. It now uses the standard slider: a thin track with a seek handle that appears on hover.
- Settings → Notifications: hovering the top-centre position on the screen picture previews notifications at their real width. Top-centre notifications are 1.5 times as wide as corner ones (480 px instead of 320 px at 100 % scale), but the preview showed corner-width ones.
- The update button in Settings → Advanced and in About now reads “Update LuxuryGram” (in Russian, «Обновить LuxuryGram») instead of “Update Telegram”, and the “was updated to version …” notice after an update names LuxuryGram rather than Telegram Desktop.
- Russian interface: the Saved Music error screen (“Couldn't load music files” / “Try again”) is translated; both strings were new to this fork and showed in English with Russian selected.
- Russian interface: 22 more LuxuryGram strings are translated, among them the forwarding progress, the expanded player, “Select messages between”, “Update LuxuryGram”, the database-error toast and the batch-translation toasts. LuxuryGram’s own Russian strings are now applied again after every start-up, so strings added by an update no longer stay in English until the language is switched off and back on.
- Russian interface: the last 78 LuxuryGram strings now have their own Russian text, so all 416 are covered. Until AyuGram's community translation downloaded, which never happens offline or on the first start, these strings appeared in English: the tray ghost and streamer items, the chat menu's deleted and filtered messages, the send confirmations, message details, creation and join dates, and the filter import sheet. When it did arrive, its wording clashed with the rest of the app: «Отключить режим призрака» in the tray next to «Включить гост-режим», «Очистить удалёнки», or «3 новых фильтров» on the import sheet. The note under a view-once video message also talked about listening to it.
- Russian interface: LuxuryGram settings rows are no longer cut off with an ellipsis. In the chat context-menu block, three rows («Сообщения пользователя», «Прочитать сообщение», «Повторить сообщение») showed «Расширенное ме…» at the default settings width; that mode now reads «С Ctrl/Shift», and the note below explains it in the same words. Eleven long titles are shorter, for example «Только мои эмодзи и стикеры», «Не предупреждать о ссылках» and «Недавние стикеры без лимита». Measured on all 199 LuxuryGram rows at 100% and 150% scale: 0 cut-off rows in Russian and English at the default and window-minimum widths (before: 3 and 4 in Russian), and 10 instead of 24 at the narrowest settings panel. The «Повторять сообщение» menu item now reads «Повторить сообщение».
- LuxuryGram → Appearance → Tray elements: switching the ghost mode or streamer mode item on or off now updates the tray menu right away. Before, the menu kept its old items until the app restarted, the account changed or the passcode lock toggled.
- LuxuryGram → Appearance → Hide Premium Statuses: premium stars and emoji statuses next to names in the chat list and in chats now appear or disappear as soon as the switch is flipped. Before, rows already on screen kept the old look until something else redrew them, such as scrolling or a new message.
- LuxuryGram → Chats → Show Only Added Emojis & Stickers: an emoji panel that is already open now rebuilds its sets when the switch changes. Before, it kept the old list until the panel was closed and reopened or the sticker sets were updated.
- LuxuryGram settings that change how messages look now apply to messages already loaded in open chats: Show Message Seconds (General), and in Chats the Deleted Mark and Edited Mark texts, Replace with Icons, Hide Side "Share" Button and Translucent Deleted Messages. Before, only the preview in settings changed; loaded messages kept their old time, marks and share button, often until the app restarted.
- LuxuryGram → Appearance → app icon grid: hovering an icon now shows the pointer cursor and a half-strength frame, so it is clear which icon a click will pick. Before, the grid gave no feedback until the click.
- Per-chat regex filters list: the title shows as much of the chat name as fits and then an ellipsis. Before, every name was cut to 17 characters even with room to spare (a long RU name filled 163 of 204 px at 100%), and a cut through an emoji left half a character.
- LuxuryGram settings rows that expand into a list of options (Ghost Mode, Hide Reactions and the other groups with a switch on the right): the divider in front of the switch now scales with the interface like Telegram's own permission rows. Before, it stayed 1 px wide at every scale, so at 200% it was half as thick as the rest of the lines.
- LuxuryGram Preferences overview: the description under the version keeps the same side margins as the settings below it and breaks into two even lines. Before, the English text ran to 13 px from the panel edges at 100% scale (settings rows start at 22 px) and left “features.” alone on the second line.
- Settings → LuxuryGram: Luxury Extra is set off from Save in Bot Dialogs by the same divider as the other blocks, instead of its title sitting in the middle of the Spy essentials block. With no watched chats, the Watched chats hint and the divider above Other no longer merge into one double grey band (6 px apart at 100% scale); the divider now closes the watched-chats list instead.
- Filters → Shadow Ban: the list of banned senders is titled Hidden Senders instead of Filters, and when it is empty it says that nobody is hidden yet and that the + at the top adds a user or bot, instead of “No filters here yet.”, which belonged to the regex filter lists.
- LuxuryGram sliders with a value next to them (Menu Corner Radius, Message Bubble Radius, Wide Multiplier) keep the track still while you drag. Menu Corner Radius used to grow and shrink its track by 7 px at 100% scale when the value crossed 10 px, with the value pressed right against the track end; every slider now reserves room for its widest value and keeps the two-space gap of the interface scale slider.
- The precise “last seen … ago” suffix no longer shows Russian units in other languages; hours, minutes and seconds follow the interface language.
- Message details menu: the sticker pack owner line no longer runs past the menu edge when the owner's name, resolved after the menu opens, is longer than the "fetching…" placeholder. Overlong file names and values are cut with an ellipsis.
- Text fixes:
  - The Russian sticker hint names the menu item as it actually reads ("Переместить в начало") instead of the English "Move to Front".
  - Russian Watcher events say "Изменил(а) имя пользователя" instead of "Изменил username", and every event about the watched person (gift, name, username, profile photo) now reads "отправил(а)", "Изменил(а)", "Обновил(а)", so it is no longer grammatically wrong for a woman.
  - The Watcher menu opens "LuxuryGram Settings" rather than "Luxury Settings".
  - The sticker pack owner "fetching…" placeholder is now translated.
- Unavailable buttons now look unavailable:
  - Message Shot: Save and Copy are dimmed while the shot renders.
  - Expanded player: Save cover is dimmed until a cover exists.
  - Message Shot theme picker: Apply is dimmed until a theme is chosen.
  - Watcher: the "Last" sort is muted where it does not apply.
  Previously these buttons looked enabled while their clicks were ignored.
- Custom forwarding: clicking the progress bar now asks “Stop forwarding?” before stopping. Messages already sent stay in the chat. A stray click used to cancel a long forward with no warning.
- Profile: the calendar button next to the ID now shows “Couldn't find the creation date.” when the date can't be found. Previously the click did nothing.
- Message field: with the emoji button turned off in LuxuryGram settings, it no longer comes back when a bot keyboard goes away, for example after the bot removes it or you switch to a chat without one. The button used to reappear over the end of the text field until the chat was reopened.
- Voice and round message confirmation: cancelling the “Send?” prompt for a recorded draft keeps it exactly as it was. Previously the draft lost its view-once mark, was cut to the trim selection, and its play button disappeared, even though nothing was sent.
- LuxuryGram settings sliders (avatar corners, message bubble radius, wide messages): clicking the slider without moving it, or scrolling back to the same value, no longer asks to restart the app.
- Message screenshot: Copy now confirms with “Image copied to clipboard.”. Previously the window just closed, which looked the same as a click that did nothing.
- Interface scale preview: message bubble corners follow the chosen bubble rounding at the previewed scale. With the interface scale above 100% the radius was scaled twice: at 150% interface and 150% preview the corners came out 36 px instead of 24 px, rounder than the chat would actually look.
- Filter import from a link: the address field is labelled in the interface language instead of a hard-coded “URL”, gets focus when you pick the option, and pressing Import with it empty highlights the field and keeps the window open instead of closing it with a “Failed to fetch filters” toast.
- Monospace font picker: pressing OK on the font already in use, or Reset when no custom font is set, no longer asks to restart the app, and the Appearance row now shows the newly chosen font right away instead of the old one until the page was reopened.
- Message filter editor: saving an empty expression now highlights the field instead of silently doing nothing, and an invalid expression is reported as “Regex syntax error: …” (translated) with the field highlighted, instead of a bare English parser code. The parser detail starts on its own line, so the error position no longer wraps onto a line by itself.
- LuxuryGram preferences: the Issues, Contributing and Documentation links no longer repeat an English copy of their own title on the right, and the General page’s untranslated “Webview” header is now a localized “Mini Apps” header.
- Ghost offline-packet mode sends one status request per explicit online event instead of polling the stale local online status every three seconds and repeatedly hitting Telegram.
- Delete-all from the context menu stops after a server flood wait instead of resending the same batch after a minute and repeating the “try again later” message indefinitely. The user can deliberately retry later.
- Gift Code dialog close icons now expose the existing localized “Close” action in both pending and active code paths without changing gift sending or payment behavior.
- The credits box close icon now exposes the existing localized “Close” action to screen readers without changing payment or balance behavior.
- Saved proxy rows now expose their existing “Proxy settings” menu action to screen readers without changing proxy routing or menu behavior.
- Premium preview close, previous-feature and next-feature icon buttons now expose localized screen-reader action names while preserving the existing Gifts branch and navigation callbacks.
- Saved Music no longer turns a failed initial load into a permanent empty playlist: the failure stays retryable, the list offers “Try again”, and an existing playlist remains available after a reload failure.
- Join-request bars refresh names and avatars when the recent applicants change without changing the pending count, instead of continuing to show the previous applicants.
- Group-call participant bars now replace stale avatars and names after a full participant reload, including same-size and empty participant lists.
- Group-call member lists now refresh mute, volume and speaking state when a full participant reload replaces an existing member, instead of retaining the previous row state.
- Shared media selection now exposes the existing multi-select state to screen readers, so selected rows remain discoverable without changing mouse or file-opening behavior.
- Shared media announces selection and deselection after mouse, keyboard, drag and batch changes; rapid changes are coalesced to the final state instead of announcing transient selections.
- The connection-state proxy action is now keyboard- and screen-reader-focusable as a button, while keeping its existing status text, retry link and proxy settings flow.
- The forum and monoforum subsection-tabs layout toggle now exposes a localized screen-reader name in every placement, without changing its layout cycle.
- History admin-log and message-history search and cancel icon actions now expose localized screen-reader names without changing search behavior.
- The history translation bar’s settings icon now exposes a localized “Translation settings” screen-reader name without changing its menu or translation behavior.
- The scheduled-message “send until online” action now exposes its existing localized action name to screen readers without changing the scheduling menu or submission behavior.
- Poll creation and poll-option media attach buttons now expose the existing localized “Attach” action to screen readers without changing media selection, upload, or link-menu behavior.
- Suggested-message options now expose the existing localized “Close” action for the custom title-row close button to screen readers without changing the dialog’s close behavior.
- The ComposeControls live/video message Stars edit action now exposes its existing localized “Edit Stars” name to screen readers without changing amount selection or sending behavior.
- The ComposeControls live/video message comments toggle now exposes its existing localized “Comments” name to screen readers without changing comment visibility or reply behavior.
- The custom Ghost profile picker rows now expose their existing account and global labels as keyboard-activatable menu items for screen readers without changing profile selection callbacks.
- The close button of the in-chat “Select theme” panel now has a localized screen-reader name.
- The close button of the Fragment collectible username and phone number info box now has a localized screen-reader name.
- The close, “More Options” and “Craft” buttons of the unique gift and gift auction boxes now have localized screen-reader names.
- The close button of the Stars subscription details box now has a localized screen-reader name.
- The back and close buttons at the top of the Premium, Telegram Business, Stars and affiliate program sections now have localized screen-reader names.
- The close button of the emoji game stake box now has a localized screen-reader name.
- The close button in the header of chat-folder invite link boxes now has a localized screen-reader name.
- The close button of the new giveaway box now has a localized screen-reader name.
- The close button of the offer to restore previously open windows now has a localized screen-reader name.
- The close button of embedded content opened from Instant View markdown now has a localized screen-reader name.
- The close button of the pinned hint tooltip in group calls now has a localized screen-reader name.
- The close button of the unique gift, gift upgrade, crafting, gift preview and Cocoon boxes now has a localized screen-reader name.
- The “add to folder” button in the toast suggesting to add a newly joined channel or group to a folder now has a localized screen-reader name.
- The age-verification prompt shown before revealing sensitive media now gives its close button a localized screen-reader name.
- In-chat message search now exposes localized screen-reader names for its cancel, previous/next result, jump-to-date and “Show messages from” buttons and for the results-list toggle, without changing search navigation or F3/Shift+F3 shortcuts.
- The ComposeControls scheduled-message toggle now exposes its existing localized “Scheduled messages” name to screen readers without changing scheduled-section navigation or composer behavior.
- Poll add-option emoji buttons now expose the existing localized “Emoji” action to screen readers without changing emoji-panel insertion or poll-option behavior.
- The screen-reader mode bar now exposes its existing Disable control to screen readers while preserving the confirmation flow and the decorative label’s hidden status.
- Returning to or reusing a saved-music list restores the loaded window, visible song and scroll offset instead of ignoring its navigation state. If the anchor song disappears from the loaded slice, scrolling falls back to a live entry rather than retaining a removed item pointer.
- The wallpaper preview’s light/dark toggle has English and Russian screen-reader action names that describe the next preview mode without implying a change to the application theme.
- The “send text as a file” icon has a localized screen-reader name matching its tooltip in both ordinary chats and the shared composer controls.
- Watcher cancels an already-satisfied deferred rebuild instead of replacing the same cards again on the next tick. Reload and clock-update callbacks stop after the viewer closes or its cards are replaced, and running-session duration layout is updated before its screen-reader announcement.
- Watcher tabs and sort controls announce both selection and deselection to screen readers after the entire choice has updated, and tolerate the control closing during an accessibility notification.
- Turning screen-reader mode on and off preserves controls’ ordinary keyboard and mouse focus behavior instead of removing existing Tab stops or weakening stronger focus policies. This applies to shared controls, including watcher actions and the main-menu links.
- Watcher records server contact-status refreshes instead of silently missing their online/offline transitions. Connection loss, MTProto session resets and sleep/time jumps break observation continuity rather than turning an unobserved gap into one long online session; sleep handling covers every signed-in account.
- Watcher rejects stale departures before a newer observed start and invalid offline timestamps instead of storing impossible session durations. A fresh online observation invalidates the previous tracked last-seen fallback, and recorded status changes refresh an open watcher without waiting for its periodic reload.
- The main menu’s LuxuryGram Preferences link supports keyboard and screen-reader activation, and long translations stay clear of the account disclosure arrow and unread badge. Account-list controls have localized action names, the disclosure reports expanded/collapsed state, and scale reset announces both 100% and the restart. The menu controls and avatar buttons show keyboard focus.
- Text links finish updating their size before announcing a changed screen-reader name, and stop safely if a resize or accessibility callback removes them.
- The photo editor’s “Keep original ratio” menu option responds to Enter and Space, and both it and “Filled shapes” expose their names and checked state to screen readers with working toggle actions. Shape menu previews update before notifying the editor of a fill change.
- Media and stories preload calculations keep a positive row-height estimate at very narrow or wide widths instead of dividing by zero.
- Emoji, sticker and GIF panel tabs support Tab focus, arrow/Home/End browsing and Enter/Space activation, with named screen-reader tabs and a theme-aware focus outline. Tab switching preserves header focus without letting the incoming search field take it; moving focus elsewhere cancels restoration.
- Emoji, sticker and GIF panels ignore an outdated tab-animation completion after closing, reopening or moving to another container, instead of running the previous transition’s shown callback on the new panel.
- Tab sliders keep the latest selection when a selection callback switches tabs again, and stop safely if that callback removes the control.
- Group-call volume menus reflect “muted for me” when reopened and keep the slider and speaker arcs in sync when mute changes at the same nonzero volume.
- Setting RTMP stream playback volume to zero stays muted after releasing the slider instead of restoring the previous volume.
- Emoji, sticker and GIF panels keep restriction messages and boost links bound to the current chat, release their animation pause when closing or moving to the side column, and no longer swipe into an empty hidden Masks tab in the photo editor.
- Collapsible LuxuryGram settings headers keep long localized titles within the row and reposition their expand arrow when the title or checked count changes.
- Settings search keeps result subtitles within their rows when the window narrows or widens, eliding long section paths and FAQ descriptions instead of clipping them at the row edge.
- Date tooltips and message-effect hit targets on GIFs, videos and round-video messages align with their painted metadata rather than sitting slightly below it.
- Message view-count tooltips follow the painted counter width and position, including silent, ephemeral and pinned indicators, instead of spilling over neighboring metadata or missing part of the count.
- The application icon picker keeps its images, selection outline and keyboard focus aligned at narrow widths and fractional UI scales; icon positions, mouse targets and horizontal arrow navigation mirror together in right-to-left layouts. Clicking the already-selected icon also refreshes keyboard focus without reapplying the icon.
- Group-call volume menus accept a newer server volume received during unmute instead of leaving the slider at a stale local preview.
- Open group-call volume menus reset to 100% when the server clears a custom volume, matching the participant list instead of keeping the old slider value.
- Stories volume controls open with the keyboard or screen readers, keep the slider visible while its controls have focus, and close with Escape without closing the story. Silent-video explanations remain available on focus without dismissing the privacy tooltip; Escape also cancels held or queued opening actions. Volume controls show keyboard focus with localized names.
- Profile and appearance tabs support Tab focus, arrow/Home/End navigation and Enter/Space activation, with a visible focus outline and named screen-reader tabs; focus stays on the same surviving tab when titles or order change.
- Retained deleted messages and opened self-destructing media expose their status in both the history screen-reader row and delivery details; opened media is not incorrectly announced as already expired.
- The shared chat composer honors the gift-button preference when opening a chat and when the preference changes; a hidden button no longer reserves input width or triggers needless relayout while typing in New chat view.
- Hiding notification counters immediately updates the other-account unread dot in both folder layouts, without waiting for another unread message; account-picker counts remain unchanged.
- Shipping choices announce the delivery option and formatted price on their radio button and clickable label area without changing checkout selection or payment behavior.
- Passport document rows expose their title, description and ready/missing/error status to screen readers, refresh those details when a document changes, and show keyboard focus.
- Statistics chart filters announce their series name and checked state, accept screen-reader toggle actions, and show keyboard focus while preserving the last-visible-series safeguard.
- Adding a contact follows the displayed name-field order with Enter, including surname-first languages and the retry form; a prefilled phone number is still skipped.
- Chat-list and search screen-reader previews honor message filters, including sender and message-detail cells; hidden content is no longer announced unless explicitly revealed.
- Painted shared-media rows in profile and global media lists now expose native list-item names, selection and focus state, keyboard browsing and photo/document activation to screen readers without adding child widgets.
- The application icon picker exposes each painted icon as a named, selectable list item to screen readers, with keyboard focus, stable activation and RTL-aware geometry matching the visual targets.
- Filtered chat-list ID-search rows now appear in the screen-reader list with their peer names, selection/focus state, and activation instead of being silently omitted.
- Reordering horizontal folders no longer restores a hidden All Chats tab or moves the wrong folder. Hidden All Chats also no longer reserves a pinned slot or an extra free-folder slot in either folder layout.
- Described settings rows participate in Up/Down navigation without losing their button/switch accessibility role. Settings activation uses each widget's keyboard handling rather than independently changing checkbox values, and respects receiver destruction.
- Locked Ghost options use the same activation guard for mouse, keyboard and screen readers. Shift+Space and a named accessibility action can lock or unlock an option, and the locked state is announced.
- Loading-disabled playback sliders ignore mouse, wheel and keyboard changes, cancel an in-progress drag and pending wheel completion, and expose their disabled state through the native widget.
- Media-viewer controls have localized screen-reader names for playback position, volume, picture-in-picture and playback settings; play/pause, mute/unmute and fullscreen labels follow the current action. Opening the viewer without speed or quality controls no longer dereferences the absent settings button.
- Compact-player controls, their fading backdrop and the label-hover area mirror together in right-to-left layouts instead of leaving the control group on the wrong side.
- Checkbox state notifications stop safely if a synchronous listener removes the widget, and screen-reader toggle actions use the same guarded checkbox activation as press actions.
- Disabled buttons ignore mouse, keyboard and direct activation, and stay disabled when hidden and shown again. Changing focus or disabling a held button cancels its pending press, and screen readers are notified when a button becomes unavailable. Arrow navigation in radio groups skips disabled choices; checkboxes require a preceding keyboard press and cancel it on focus loss or hide. View-once voice/video recording keeps its off/on state separate from availability, remains clickable when off, and exposes that state to screen readers.
- Settings descriptions are available to screen readers, icons mirror correctly in right-to-left layouts, and reactive layout updates are coalesced before painting. Repainting the preferences logo reuses its scaled image until the icon, display ratio or logical size changes; switching display ratios also refreshes the source logo, and PNG previews retain their physical resolution on high-DPI displays.
- Opening an encrypted cache for replacement acquires its lock before truncating it; a failed lock attempt leaves the existing data intact. A short or failed header-marker write now aborts opening the cache.
- Cache compaction stops after a startup failure. Promoting a compacted cache index no longer truncates or deletes the existing index before the replacement succeeds.
- Code-block highlighting separates text length from the language in its cache key, preventing different text/language pairs from sharing an identical hash input.
- Embedded Windows webviews stop querying navigation history after a navigation callback closes their controller.
- Embedded ICU data is generated byte-for-byte with explicit alignment instead of silently producing an empty initializer; empty data fails configuration.
- Style and localization generation keeps comment-like text inside quoted strings after odd backslash runs, and no longer reads before the input buffer when a quote follows its first backslash.
- Saved deleted messages survive upgrading older or partially migrated databases. Existing message and media fields are preserved, and an incompatible schema, unexpected generated columns or failed migration no longer triggers destructive table replacement.
- Edit history is saved again on databases carried over from AyuGram. Their edited-message table lacked the channel post-author column, so every edit failed to save; the table is now converted once on startup with its existing rows kept.
- Published product versions and versioned updater packages cannot be overwritten by rerunning the release workflow; unpublished drafts may receive missing assets, but existing asset names are never replaced.
- Canary builds discover updates in their configured public or enrolled private channel instead of polling the stable feed, and reject signed packages from a different lane.
- Protected Linux installations accept signed LuxuryGram stable packages and require the canonical LuxuryGram and Updater files, while keeping installation trust and symlink checks intact.
- Canary builds generate the nonsecret Packer headers in the location used by all platforms; this Packer accepts only v2 packaging and cannot sign legacy updates.
- Automatic updates use the bounded unpacker for signed legacy packages again, including explicit legacy-alpha version handling. Failed version-marker writes no longer mark an update ready.
- Message Shot cancels pending rendering and releases its message views before selected messages or session data are destroyed. Anonymous forwarded headers are restored before delivering a screenshot to the box.
- Chat-specific regex filters stay active when Shared Filters in Chats is turned off. Album membership changes and caption, button or media edits invalidate cached filter results, and repeated filter imports release temporary compiled patterns.
- Turning off ads no longer suppresses ordinary account suggestions, custom suggestions or their refresh notifications.
- Hiding All Chats no longer switches a valid selected folder back to the first folder during a sidebar refresh.
- Downloaded custom translations refresh existing labels as one batch. Language switches cancel obsolete downloads and load the new language's matching cache, preventing an old response from overwriting the current language.
- “Send With Sound” now bypasses the ghost-mode silent default for that message without changing the account setting. Broadcast and support-mode silence still take priority, and the menu no longer offers sound when those restrictions apply.
- Custom forwarding waits for its own outgoing messages rather than unrelated sends in the same chat. Cancelling a job prevents deferred file preparation from starting another send.
- Rich-message forwarding follows the original session through loading, uploading and sending; cancellation stops pending requests and late callbacks cannot access a closed account. File blocks are reuploaded like other media, and suggested-post options reach the server.
- Custom forwarding finishes promptly on terminal preparation, upload or send failures; successful paid albums complete every tracked file instead of leaving the job waiting.
- Custom forwarding preserves an explicitly selected future send time and repeat interval instead of sending immediately or using the automatic ghost-mode delay.
- Reordering attachments rejects malformed or out-of-range drag indexes before reading the file list.
- Unlimited message selection no longer reserves space for the entire policy limit when opening a context menu; allocation is bounded by the actual loaded message span.
- GIF confirmation also applies to search results, preserving the chosen send options and result while the confirmation is open.
- Shift Repeat sends a webpage message as text with its link preview rather than sending the preview artwork as an attachment or dropping a preview without artwork.
- Rich-message translation follows the selected provider: Google and Yandex use the text fallback instead of bypassing that choice through Telegram's rich-message API.
- Yandex translation sends the original plain text, preserving literal symbols and line breaks without inserting HTML markup that could appear in the result. Plus signs and percent sequences reach the translator as written: "C++" and "+7" no longer come back as "C" and "7", and a literal "%41" is no longer translated as "A".
- Switching translation providers cancels pending downloads, queued batch items and retry timers; obsolete results can no longer refill the cleared cache.
- The saved-music cover menu closes with its row and resolves the current message before refreshing it, avoiding stale callbacks when the song or profile disappears.
- The expanded player clears the previous track's cover and disables Save Cover while the new cover loads, including tracks with no cover.
- Message Shot applies a theme's palette, identity, accent and name together. Apply stays unavailable while the latest cloud theme is loading, and obsolete theme downloads no longer overwrite a newer choice.
- Message Shot Save and Copy use the completed image for the current preferences, independently of preview animation. Changing anonymity, spoilers or other preferences disables export until that render is ready.
- Ghost-mode controls refresh when only locked options change, keeping the aggregate toggle up to date.
- Starting with `-ghost` applies privacy restrictions even when custom settings are missing, unreadable or oversized, and also covers individual account profiles before their first use.
- Suggested ghost mode for stories restores the previous flags of the original profile when the story closes, preserving current locks and intervening edits instead of enabling every packet type.
- The tray ghost toggle rebinds when switching accounts and refreshes the current profile whenever its menu opens, including after resetting custom preferences.
- Local Read and Server Read now retain their explicit intent through delayed forum and chat-summary work without temporarily changing ghost-mode settings. Local Read clears local mentions/reactions without sending server mutation requests.
- Ghost-mode local reading no longer discards unrelated in-flight deletion, history loading or chat-summary requests. Read-on-interaction and Read Up To release tracked requests on both success and failure.
- Deleted-message history finds Watcher's saved media by the original message ID while retaining separate IDs for viewer items.
- Watcher retains media deleted before its photo write or document download has finished, and retries the keep only after the write completes.
- New Watcher media files are isolated by account and typed chat identity. Clearing one account's chat cancels its pending saves and downloads, does not erase another account's files, and does not delete saves started afterward. Older files without account information remain in `tdata/luxury_media/pending/` and `kept/` for manual recovery; they are not assigned to an account or exposed automatically.
- macOS updates locate the helper inside the LuxuryGram app bundle, and the Linux updater replaces the running executable even when it has been renamed.
- Text fields with a length limit now enforce it for text committed through an input method (IME, on-screen or handwriting keyboards, dictation) as well. Previously such input went past the limit (80 characters went into a 64-character deleted/edited mark) while ordinary typing stopped at it.
- On the login screen, the LuxuryGram version line no longer covers the terms of service note when signing up or the Reset Account button during two-step verification. Those controls now sit above the line. Before, they overlapped it by 10 px at 100 % and 12 px at 150 %. When the window is too short to leave room, the version line is hidden, so it also no longer covers the login content (at 200 % it overlapped the passkey login link).
- Settings → LuxuryGram → Other no longer shows a Crash Reporting switch in builds with auto-update (the published releases). LuxuryGram has no crash collector, so the crash window never offers to send a report: the switch changed nothing, and its description promised a prompt that never appeared. The page now opens with Register URL Scheme and Reset Settings, as in builds without auto-update. The switch and its section return automatically if a collector is ever configured.
- In the ⋮ menu of a sticker or emoji pack you created, Pack Owner and Copy ID now sit above Delete Pack and the "Check the @stickers bot for more options" note. Before, they came after that note and split it from the separator above it, so the note no longer closed the menu.
- The expanded media player now has a close button in its title bar, like other LuxuryGram boxes. Before, its only button was Save cover, which stays disabled until a cover loads, so the box showed no visible way to close it. It now also opens in the window that holds the player instead of the last active main window, and no longer closes a box that is already open there.
- Ghost mode's "Schedule Messages" no longer breaks quick replies, welcome messages and the files box of the Scheduled messages section. A message added to a quick reply was sent with a schedule date and showed up among the scheduled messages of Saved Messages instead of in the quick reply; a file added to a welcome message showed up among that chat's scheduled messages while uploading; files added in Scheduled messages were sent at least 19 seconds later instead of asking for the date. Now quick replies and welcome messages are never scheduled, and the files box asks for the date as it does with ghost mode off.
- Ghost mode's "Schedule Messages" no longer schedules self-destructing media. View-once voice and round messages, and photos or videos with a timer, went out with a schedule date a few seconds ahead, which Telegram does not allow for such media; the files box already skipped them, but the upload put the date back. They are now sent right away, as with ghost mode off.
- Publishing chat filters to a link (Filters → Export → "To link") no longer says a sticker pack link was copied. The toast reused a sticker-pack key whose Russian text reads "Ссылка на набор стикеров скопирована."; it now reads "Ссылка скопирована в буфер обмена." like the other link copies, English is unchanged.
- Mini apps opened from the attachment menu now follow ghost mode's "Send without Sound". A message such a mini app sent on your behalf went out with sound; now it goes out silently whenever your own messages do.
- The About box no longer offers "Check for updates" when "Update automatically" is off in Settings → Advanced. In that state the updater ignores checks, so the click did nothing and the status line stayed empty; Settings already hides its check button then. While an update downloads, the status line in About now reads "Downloading update 12.5 / 34 MB..." as in Settings, instead of the bare figures.
- Clicking a row in Filters → Shadow Ban to lift that ban no longer rebuilds the page while the click is still being handled. The rebuild deleted the list the click came from, and the rest of the click then read freed memory, which could crash the app. The page is now rebuilt right after the click finishes.
- "Hide Similar Channels Tab" now also hides similar channels in global search. Before, the switch only removed the tab in channel profiles, and the search tab still loaded and listed recommended channels.
- Typing in the message field with the auto-delete button turned off no longer re-lays out the field's buttons on every keystroke in chats that have an auto-delete timer.
- Sticker and GIF confirmation now asks only in the chat panel, where the choice sends a message. It no longer pops up when a sticker is picked in the photo editor, when adding a sticker to your own pack, in the business chat intro, or in a poll.
- The timer icon on a self-destructing round video is drawn at full size again; it was a third of its size and off center.
- Closing a chat with a self-destructing voice message no longer leaves its timer animation repainting the removed message.
- "Spoof Client as Android" now also covers the mini app a bot opens for a join request; it was the only mini app still reported as Telegram Desktop.
- "Increase Content Height" and "Increase Content Width" now also apply to mini apps opened in a separate window on Linux, not only to the embedded panel.
- Message Shot now works in a channel's Welcome Message section (Manage → Welcome Message): the Shot button in the selection bar did nothing there.
- Music sent from the music picker now follows ghost mode's "Schedule Messages", quotes a deleted message it replies to instead of failing, and sends premium emoji in its caption the same way as other captions. Rich messages now follow "Schedule Messages" too.
- With deleted messages saved, the media viewer no longer warns that a view-once photo or video "can be opened only once": the kept copy stays, so it now says it can be viewed as many times as you want.
- Custom forwarding of a rich message (an article with blocks) now follows ghost mode's "Schedule Messages" and no longer re-applies the effect of the original send. In a chat that charges Stars, or as a reply to a deleted message, the page is now sent as a rich message; before, the server refused it and the page went out as plain text. A button that cannot be re-sent, such as a callback button, is now dropped from the page instead of turning the whole page into plain text.
- With "Attach" hidden from the message field, editing a message no longer draws the replace-media button over the start of the text; the field now leaves room for it.
- Saved deleted messages stay in a forum topic after you switch to another topic and back, and the same holds for Saved Messages chats and channel direct messages. Before, the topic reloaded its messages from the server, which no longer returns deleted ones, so they were visible only in "View as Messages" or "View Deleted".
- Ghost mode's "Schedule Messages" now also covers inline bot results (picked from `@bot query`), forwards (from the message menu, the share box and the forward panel), polls, checklists, locations, venues, shared contacts and a dice sent with "Send" from the toast of a dice tapped in the chat. Before, these went out at once and showed you online. A dice with a TON stake is still sent at once, so the stake stays on the game it was placed on. Forwards of an ephemeral bot reply and a contact or location sent to an ephemeral bot are still sent at once, because a scheduled message to them goes out as an ordinary one.
- Saving a public channel's settings no longer fails with `CHAT_ID_INVALID` when join requests were left on from when it was private. Opening "Channel type" and pressing Save without changing anything used to ask whether to turn join requests off for the channel's invite links, then try to turn them off and stop the whole save with that error. The hidden "Approve new subscribers" setting of a public channel is now kept as it is.
- A translation bar hidden with "Hide" can be brought back with "Translate" in the chat's menu without Telegram Premium. LuxuryGram offers chat translation without Premium, but the menu item that undoes "Hide" still required it, so once the undo toast was gone the bar stayed hidden for good.
- The Photos, Videos, Files, Links and other shared media counts in a profile no longer include messages that were deleted while "Save deleted messages" kept them in the chat. The kept message stays in the chat, but it no longer shows up in the profile's shared media lists or counts.
- Changing the app icon, or the first start of a copy with a new icon, no longer gives the LuxuryGram icon to AyuGram's shortcuts or to those of another LuxuryGram copy. Shortcuts were picked by name, so a pinned "AyuGram" or a Start menu "AyuGram" entry got our icon too. Windows is now asked to refresh icons only when one of this copy's shortcuts really changed. Before, every icon change made Explorer redraw all icons on the desktop, even for a portable copy with no shortcuts at all.
- "Adaptive layout for wide screens" in Chat Settings → Chat background is always shown. Before, it appeared only while the chat area was already wider than 880 px at 100% scale. In a narrower window it was missing from settings, and its settings search result opened Chat Settings without showing it. The setting takes effect once the chat area is that wide.
- The unread counter on the tray icon and on the window's title-bar icon is readable again at 200% and 300% display scale. Before, it shrank into a small red corner in the bottom-right of the icon, or did not show at all on the 20 px tray size and on the big window icon, because the LuxuryGram logo was drawn at half the size the counter was laid out for. At 100% nothing changes.
- The message-shot box now falls back to the chat's own theme and clears the saved choice when a saved custom theme can no longer be loaded (its file failed to download or parse, or it has no theme file), instead of staying stuck waiting for an apply that never comes.
- Partial user updates that carry no last-seen information (for example a name or photo change) no longer reset the online-presence tracker, so online intervals recorded by Watcher stay continuous instead of being split by unrelated updates.
- Online history now records live Telegram status updates as well as user snapshots, independently of the temporary online status inferred from messages or typing. Repeated server confirmations no longer duplicate transitions, and an initial offline snapshot is not recorded as a departure.
- Watcher no longer labels old unfinished sessions as Online now. Missing departures and observation gaps remain explicit; resumed tracking starts a new interval instead of merging time across pauses or restarts. Same-second transitions and system-clock changes retain insertion order.
- Live name and username updates are now included in Watcher events rather than being missed until another user snapshot arrives.
- Clearing Watcher history also clears its cached last-seen fallback and active-session state instead of continuing to display cleared data.
- Bulk custom forwarding no longer aborts the whole job when one message has no forwardable content, its media cannot be prepared, or its media fails to download: such items are skipped, their text is still sent when available, and the rest of the selection completes. The forward progress bar counts only what can still be sent and appends a "skipped N" note when items were dropped.
- Re-sent (repeated) and bulk-forwarded messages no longer inherit the source message's scheduled date, message effect, reply suggestion or media-replacement context; each forwarded item is sent as a fresh message.
- Translating multiple messages no longer stalls the batch when a translation attempt completes twice or a service reply completes synchronously: each attempt is counted exactly once and the queue keeps draining to the next message.
- Ghost-mode settings changes made from background threads now run on the main thread instead of writing concurrently with the autosave timer, which could interleave two writers.
- Ghost-mode per-account settings are capped at 64 accounts instead of growing without bound.
- The Windows pinned-shortcut scan no longer leaks a file-search handle when an error occurs.
- User registration-date lookups skip entries whose date cannot be parsed instead of showing a broken date.
- Watcher gear popup: the tracking toggle was labeled "Record in all chats", implying a per-chat scope that does not exist; it now shows the same "Track Online History" label as the settings page (same switch).
- Watcher session list: holding a press no longer makes the list drop incoming updates; rebuilds wait for the press to end and then apply, so the list never goes silently stale.
- Watcher saved-attachment ok marker: the hard-coded green is replaced with the theme's online-status color, so custom themes no longer get a dot that matches nothing.
- Watcher session cards now expose their content (state, duration, open/closed) to screen readers via accessible names.
- Settings boxes that previously fell back to English for Russian users now show Russian labels: the font customizer, the message-shot theme picker, the chat-filter import/export options, and the plugin details box.
- Watcher online-history departures now record the server's exact last-seen time instead of the moment the update arrived, so recorded offline events keep their real transition time.
- Star gifts forwarded into Saved Messages no longer create a duplicate Watcher gift event; the forwarded copy is matched to the original gift message.
- Watcher reaction events now skip bot and service chats, matching the gift and profile-change exclusions; reaction events in groups and channels are unchanged.
- The Russian label for the watcher media switch said "Не отслеживать", implying history recording stops; it now says "Не скачивать медиа", matching the switch's actual effect of stopping media downloads.
- Watcher event cards whose kind is not recognized now show a dash instead of an empty label.
- Rich messages (for example web page previews) no longer risk freezing the app when a relative date such as today rolls over.
- Regex filter lists show a "Loading…" placeholder while the stored filters are being read, instead of presenting a list that already looks empty.
- The Shadow Ban context-menu action now confirms the new state with a toast, instead of toggling silently while messages from that sender are hidden or restored.
- Filter list rows whose peer cannot be resolved now show a localized "Unknown (ID: …)" name instead of a hard-coded English label.
- The Longest sort option stays visible but disabled on the Watcher Events tab instead of disappearing, and a stored Longest choice is re-applied when you return to Sessions instead of being silently replaced by Newest.
- Hovering a watcher event shows the exact date and time in one consistent format: the tooltip no longer disagrees with the compact label when the label drops the date part.
- The Watcher list no longer relayouts every second while active-session durations tick; only a real geometry change triggers a relayout, so scrolling and selection stay stable.
- Sort and tab segments whose labels do not fit now show the full text on hover instead of cutting it off with no way to read it.
- The saved deleted-messages and edit-history lists no longer greet an empty list with a search-style "No results" line: the standing empty state explains that messages will appear once "Save Deleted Messages" (or "Save Edits History") is on, and searching now reports that nothing matched the query it searched for.
- Watcher empty and loading text blocks are now exposed to screen readers instead of being painted text that assistive technology cannot see.
- The Watcher sort and tab toggle rows now report their real on/off state to screen readers instead of always announcing themselves as checked.
- The Watcher session card's accessible name keeps updating while the session duration ticks, so screen readers hear the current duration instead of the value from when the card was created.
- The Settings sample rows — the message-bubble preview, the avatar-corners chat row and the "no fonts found" message — now follow the interface language instead of always showing hard-coded English text.
- Message history rows whose sender cannot be resolved show the same localized "Unknown (ID: …)" fallback as the filter lists, and the filter rows' placeholder avatar now carries the localized first letter instead of a hard-coded "U".
- Selecting a message-shot theme whose file fails to download or parse now shows a "Could not load the theme file" toast instead of silently selecting nothing.
- The avatar-corners sample row in Settings is now reachable with the keyboard — Tab focuses it, Enter or Space opens the releases page — draws a focus ring while focused, and announces its purpose to screen readers instead of being mouse-only.
- Custom `luxury://` section links that point to a section this build does not know now show a localized "Unknown section." toast instead of the developer placeholder ":3".
- A message shot rejected as too large now explains itself with "Sorry, the screenshot is too large." instead of reusing the passport upload error "Sorry, this file is too large.".
- Message details now show a localized "Unknown" in the DC field when the datacenter cannot be determined, instead of the raw `DC_UNKNOWN`/`UNKNOWN` markers.
- Refreshing the app icon now rewrites every pinned taskbar shortcut it recognizes, including pins saved under the LuxuryGram names; before, only a single legacy-named pin was ever updated.
- The app icon picker's clickable area now matches the drawn icon; before, the hit rectangle was offset up and to the left of the visible image.
- The app icon picker is now reachable and operable from the keyboard: Tab focuses the grid, arrow keys move between icons, Enter or Space applies the selection, and a focus rectangle marks the active cell.
- The saved-music button now paints its press ripple with adaptive cover colors enabled, not only with the flat cover fill.
- Saved-music track titles and performer names now sit one padding away from the cover image, matching where the cover is drawn, instead of being pushed out twice as far.
- The message-shot box title now updates when the interface language changes instead of keeping the wording from when the box was opened.
- A voice (or round video) recording left paused in the record bar when the automatic app lock triggers is no longer silently lost after unlocking: the paused draft survives the unlock and can be resumed, while explicitly discarding it (trash, cancel) still works at once.

### Security

- Filter-hidden incoming previews are suppressed at the chat-list/search accessibility boundary as well as visually; raw message text, sender and reactions no longer bypass the filtering choice through screen-reader cells.
- The style/language code generator no longer reads before the input buffer when it begins with a backslash followed by a quote.
- Damaged ghost-mode settings retain valid privacy restrictions instead of resetting the entire account profile. The original settings file is preserved for recovery, and saving is disabled if that recovery copy cannot be created.
- Local Read does not send read receipts or mention/reaction changes to the server, even when dispatch is delayed beyond the menu action.
- Streamer mode hides context menus from screen capture again. Since the 7.1.5 upstream sync, a menu opened during a stream was visible to the recording.
- Streamer mode and Telegram's screenshot protection no longer reveal each other's windows to screen capture. Previously, closing a one-time voice or video message made every LuxuryGram window recordable even with streamer mode on. Turning streamer mode off also exposed protected content still on screen, and opening media from a channel that forbids saving did not keep the viewer protected.
- Streamer mode now hides every LuxuryGram window from screen capture, not just the main window, media viewer, picture-in-picture, notifications, Instant View and menus. Bot mini apps, payment forms, Telegram Passport, chat export, call windows and tooltips were visible to the recording before. With streamer mode saved as on, they are hidden from the first start too.
- Ghost mode's "Don't Send Typing" now also covers animated emoji. Tapping one in a private chat, or watching one the other person tapped, used to tell them "watching 🎉" or play your tap on their screen even with the option on; the animation still plays for you, and nothing is sent.
- Ghost mode's "Don't Read Messages" now also covers channel direct messages on the admin side. Opening a subscriber's conversation or marking it as read sent them a read receipt even with the option on; now it is not sent, as in chats and forum topics.
- "Disable Open Link Warning" now skips the confirmation only for http and https links. A hidden link to a local file, a network share, a Windows search or another local handler always asks before opening.
- Links whose scheme the system would hand to a local handler instead of a browser are no longer opened at all: file:, network shares (smb:, nfs:, webdav: and similar), Windows search (search-ms:), shell:, ms-* handlers, script hosts (javascript:, vbscript:), Java and help launchers, and drive-letter paths. Telegram's own tg:, internal and TON links are unaffected.
- Links in a plugin's description are limited to http, https and tg; any other link is shown as plain text.
- The bundled OpenSSL was updated to the 3.5.9 LTS release, staying on the maintained fixes branch.

## 1.0.19

### Changed

- Watcher uses a wider, theme-aware reading layout: quiet controls, selectable message text, separate before/after blocks, message identifiers and an explicit Open message action. Decorative neon rims and repeated card entrance animations are removed.
- Long event details fold by their rendered height, not a character limit. Show full text / Collapse text are explicit buttons; line breaks are preserved and the list relayouts around the full content.
- The inactive Longest sort option is hidden on Events. Show more records now reveals the rows that previously appeared only as an unreachable earlier-records count.
- Watcher uses the standard box scroll area with pinned tabs and sorting, rather than a fixed-height nested scroller. Switching tabs or sort order starts at the top of the list.

### Fixed

- Watcher text height now uses the actual available width; long paragraphs no longer paint outside the height reserved for them.
- Edit events no longer label a previous snapshot as the current version or collapse repeated changes into a misleading original-to-previous comparison. New edits retain both before/after texts; older records show each recoverable change and clearly identify unavailable/current-only data.
- Reaction events retain the complete message text for new events, including line breaks; removing the last reaction is also recorded. Old records keep their saved context; missing context falls back to explicitly labelled current text when available.
- New custom reaction records use their available emoji instead of a bare "custom" label; an unavailable emoji is identified by its document ID.
- Watcher tabs, sorting and tools use the toolkit button input path instead of bespoke press-only activation, with visible keyboard focus and cancelled-click behavior. Keyboard activation no longer fires twice when the pointer also hovers the button.
- Events observed in the same second retain insertion order instead of shuffling reaction/edit transitions on reload.

## 1.0.18

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

