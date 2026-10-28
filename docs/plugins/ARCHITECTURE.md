# LuxuryGram plugin system — architecture contract

Status: phase 1 (JavaScript). Phase 2 adds Python on the same host API.

Writing a plugin: [AUTHORING.md](AUTHORING.md), typings in
[lux.d.ts](lux.d.ts), a working example in
[examples/hello-world](examples/hello-world/).

## Goals

- Install a plugin by pasting a GitHub link. Update it from the same source.
- Plugins can hook messages, extend the UI, call any Telegram API method,
  use the network and keep their own storage.
- A plugin gets only the permissions its manifest declares and the user
  approved. The session key, passcode, local database and other plugins are
  never reachable from plugin code.
- A broken or hostile plugin cannot freeze or crash the client: CPU and
  memory limits, errors are caught and shown in the plugin manager.

## Layout

All plugin code lives in `Telegram/SourceFiles/luxury/plugins/`.

| Part | Files | Qt | Owner |
| --- | --- | --- | --- |
| Core model | `core/plugin_manifest.*`, `core/plugin_permissions.*`, `core/plugin_source.*`, `core/plugin_registry.*`, `core/plugin_version.*` | no | worker `plugins-core` |
| TL codec | `core/tl_schema.*`, `core/tl_codec.*`, schema resource | no | worker `plugins-tl` |
| JS engine | `js/js_engine.*`, vendored `luxury/libs/quickjs/` | no | worker `plugins-js` |
| Host | `plugin_manager.*`, `plugin_installer.*`, `plugin_host_api.*`, `plugin_hooks.*`, client hook call sites | yes | worker `plugins-host` |
| UI | `luxury/ui/settings/settings_plugins.*`, `luxury/ui/boxes/plugin_*_box.*`, strings | yes | worker `plugins-ui` (after host) |

"No Qt" parts use only the standard library and `luxury/libs/json.hpp`
(nlohmann). They compile with plain g++ in `tools/tests/` fixtures.
Strings are UTF-8 `std::string`.

`Telegram/CMakeLists.txt` is edited only during integration: every worker
reports the files it added.

## On-disk layout

```
tdata/luxury_plugins/
  registry.json              installed plugins, grants, enabled state, source
  registry.json.bak          the registry before the last write
  registry.json.bad          a broken registry.json, kept for inspection
  plugins/<plugin-id>/
    code/                    downloaded files, read-only for the plugin
    code.prev/               the version before the last update (rollback)
    code.old/                replaced files while a swap is in progress
    pending.json             swap journal {ref, fresh, rollback}, see below
    .starting                exists while the plugin is starting
    data/storage.json        lux.storage (quota 5 MiB)
    data/settings.json       values of manifest settings
  .staging/<random>/         install/update in progress, renamed into place
```

Plugin folders live under `plugins/` so no id can name `registry.json`,
`.staging` or another file of the manager. Paths reach `std::filesystem`
as UTF-8 (`std::u8string`), never through the local 8-bit code page, and
a file system error during the swap is reported as an install error.

Install and update write into `.staging` first and swap directories only
after every file was downloaded and its size checked. A failed update keeps
the old version running.

Recovery at startup, before any plugin runs:

- `registry.json` is written with `QSaveFile`; the previous file is copied
  to `registry.json.bak` first. A `registry.json` that does not parse is
  renamed to `.bad` and the registry is restored from `.bak`; a missing
  `registry.json` is a fresh start.
- The swap writes `pending.json` before it renames `code` to `code.old` and
  the staged files to `code`. Once the registry names the new version,
  `code.old` becomes `code.prev` and the journal goes. A journal found at
  startup is finished when the registry already holds its ref, otherwise
  `code.old` is put back (or a fresh install is removed). A rollback
  journal (`rollback: true`) first moves `code` back to `code.prev`, so an
  interrupted rollback loses neither version.
- `.starting` is written right before a plugin's engine starts and removed
  on the next event loop turn. Found at startup, the client died starting
  that plugin: it is disabled with a "closed unexpectedly" error the
  Plugins page shows. The registry stores the marker `luxury:quarantined`
  as `lastError`; `PluginView::lastError` shows it in the UI language.
- `startAll` starts the plugins one per event loop turn, and a start
  removes the `.starting` another plugin still holds (`StartFence`), so
  there is never more than one sentinel and a crash disables only the
  plugin that caused it.
- `-noplugins` on the command line, or Shift held when `startAll` runs,
  keeps every plugin stopped for that launch without changing their
  enabled state. Plugins start only after the first window is shown.

`Manager::rollback(id)` swaps `code.prev` back with the same journal and
restores the previous registry source; grants never widen on a rollback.
A pinned plugin (`setPinned`) is skipped by update checks and refuses
`prepareUpdate`. An install of an older release than the installed one
needs `InstallPlan::allowDowngrade`; an update never downgrades, and
`prepareUpdate` of the installed version fails with `ErrorCode::UpToDate`.
`commitInstall` and `rollback` report an `ErrorCode` with their text:
`Busy` while an install of the plugin runs, `Consent` when the plan needs
the consent box again, `Conflict` when the plugin list or repository
changed, `Files` when the swap failed, `None` on success.
`cancelInstall(id)` marks a `commitInstall` still fetching: the requests
in flight are not aborted, and when the current fetch (`plugin.json` or the
whole file download) ends it finishes with `Busy` before anything is
written or swapped; once the swap starts it runs to the end.

Plugin log lines are mirrored into the client `log.txt` as
`Plugin <id> [level]: text`, one line each (line breaks become spaces), at
most 100 lines a minute per plugin. `logAdded()` fires the plugin id for
every new line, so an open details box follows the log live.

## Manifest: `plugin.json` at the repository root (or the linked subdir)

```json
{
  "id": "dev.alice.autoreply",
  "name": "Auto reply",
  "version": "1.2.0",
  "description": "Answers while you are away",
  "author": "alice",
  "api": 1,
  "runtime": "js",
  "main": "main.js",
  "files": ["main.js", "lib/text.js"],
  "permissions": ["messages.read", "messages.send", "ui", "storage"],
  "network": ["api.example.com", "*.example.org"],
  "settings": [
    {"key": "text", "type": "string", "title": "Reply text", "default": "Away"},
    {"key": "onlyPrivate", "type": "bool", "title": "Private chats only", "default": true},
    {"key": "delay", "type": "int", "title": "Delay, s", "default": 5, "min": 0, "max": 600},
    {"key": "mode", "type": "select", "title": "Mode", "default": "all", "options": ["all", "contacts"]},
    {"key": "ratio", "type": "number", "title": "Ratio", "default": 0.5, "min": 0, "max": 1, "step": 0.1},
    {"key": "accent", "type": "color", "title": "Accent", "default": "#3390EC"},
    {"key": "token", "type": "secret", "title": "API token", "placeholder": "Paste the token",
     "description": "From example.com/settings",
     "locales": {"ru": {"name": "Токен API", "description": "Со страницы example.com/settings"}}}
  ],
  "locales": {"ru": {"name": "Автоответ", "description": "Отвечает, пока вас нет"}},
  "minClient": "1.0.22",
  "homepage": "https://github.com/alice/autoreply"
}
```

Rules:

- `id`: `^[a-z0-9][a-z0-9._-]{1,62}[a-z0-9]$`, no `..`, and not a Windows
  device name (`con`, `prn`, `aux`, `nul`, `com0`-`com9`, `lpt0`-`lpt9`,
  with or without an extension). It is a directory name: Windows drops a
  trailing `.`, so the last character must be a letter or digit. Unique key
  on disk and in the registry.
- `api`: integer plugin API major. The client supports `kPluginApiVersion = 1`.
  A larger value is refused with a clear message.
- `runtime`: `"js"` now. `"python"` (WASM sandbox) and `"python-native"`
  (trusted, separate process) are parsed and refused as "needs a newer
  client" until phase 2.
- `files`: every file to download, relative paths only. Default `[main]`.
  Max 200 files, 20 MiB total. `IsSafeRelativePath` rejects `..`, absolute
  paths, backslashes, drive letters, empty segments and NUL.
- `network`: required when `network` permission is requested. Hosts or
  `*.suffix` wildcards. `"*"` means any host and is shown to the user as such.
- `settings[]`: `key` (1-64 of `[A-Za-z0-9_.-]`, unique), `title` and
  `type` are required; `description`, `placeholder` and `locales` are
  optional. The settings box shows `description` under the field,
  `placeholder` inside an empty text field (`title` then sits above it) and
  the setting's `locales` entry for the client's language. Types and their
  `default`:
  - `string`, `secret`: a string, default `""`. A secret is never logged
    and the settings box masks it like a password field.
  - `bool`: a boolean, default `false`.
  - `int`: an integer within optional `min`/`max`, default `min` or `0`.
  - `number`: a finite number within optional `min`/`max`, default `min`
    or `0`; `step` must be positive and is an input hint, not a
    constraint.
  - `select` (alias `choice`): `options` is a non-empty array of distinct
    strings; the default must be one of them, else the first option.
  - `color`: `#RRGGBB` in either case, default `#000000`.
  `lux.settings.get` returns the stored value when the setting still
  accepts it (`SettingAccepts`), otherwise the default.
- `locales`, top level and per setting: `{"<lang>": {"name", "description"}}`.
  A language code is 2-16 of `[a-z0-9-]`, starts with a letter and does not
  end with `-` (`ru`, `pt-br`). A setting's `name` translates its `title`.
  The settings box picks the client's language id, then its base language
  (`pt` for `pt-br`; `FindLocale`), else the manifest text. The top-level
  entry is parsed and validated only: nothing displays it yet.
- Errors name the field path and, for the id and colors, echo the value:
  `settings[2].default: must be one of options`,
  `settings[0].locales.RU: must be a language code like "ru" or "pt-br"`,
  `id: must match ^[a-z0-9][a-z0-9._-]{1,62}[a-z0-9]$ without ".." or a device name, got "My_Plugin"`.
- Unknown keys are ignored (forward compatible). Wrong types are errors,
  and so is an unknown setting `type`: settings are parsed before
  `minClient` is compared, so a client older than a setting type refuses
  the manifest with `settings[i].type: unknown type`.

## Permissions

| Id | Grants | Dangerous |
| --- | --- | --- |
| `messages.read` | every incoming message of the account, in every chat, plus the message passed to menu actions; edits, deletions and reads; `lux.chats`, `lux.account.me`, `messages.get`, `history`, `downloadMedia` | |
| `messages.send` | `lux.messages.send`, `sendFile` | yes |
| `messages.modify` | rewrite or cancel outgoing text, its entities and reply | yes |
| `messages.manage` | `lux.messages.edit`, `remove`, `forward`, `markRead` | yes |
| `ui` | commands, menu items, hotkeys, toasts, notices, dialogs | |
| `network` | `lux.http.fetch` to manifest hosts only | |
| `storage` | `lux.storage` | |
| `clipboard` | read and write the clipboard, within 1 s after the user runs a command, menu item or hotkey of the plugin | yes |
| `telegram.api` | `lux.api.call` raw MTProto methods | yes |
| `telegram.updates` | raw update stream | yes |

Even with `telegram.api` these methods are always refused
(`IsMethodForbidden`, case-insensitive):

- whole namespaces: `auth.*`, `wallet.*`, `smsjobs.*`, every `invoke*`
  wrapper and `initConnection` (only the client may wrap a query);
- `payments.*` except `payments.get*` reads, and of those `getSavedInfo`,
  `getBankCardData`, `getPaymentForm`, `getPaymentReceipt` (saved name,
  phone, email and address) and the withdrawal and ads account URLs;
- sessions and takeout: `account.getAuthorizations`, `getWebAuthorizations`,
  `resetAuthorization`, `resetWebAuthorization(s)`,
  `changeAuthorizationSettings`, `setAuthorizationTTL`,
  `invalidateSignInCodes`, `initTakeoutSession`, `finishTakeoutSession`,
  `deleteAccount`;
- password and recovery: `account.getPassword*`, `updatePasswordSettings`,
  `getTmpPassword`, `resetPassword`, `declinePasswordReset`, the password
  email methods;
- login email and phone: `account.sendVerifyEmailCode`, `verifyEmail`,
  `changePhone`, `sendChangePhoneCode`, `sendConfirmPhoneCode`,
  `confirmPhone`, `sendVerifyPhoneCode`, `verifyPhone`;
- Telegram Passport: `account.getAllSecureValues`, `getSecureValue`,
  `saveSecureValue`, `deleteSecureValue`, `getAuthorizationForm`,
  `acceptAuthorization`;
- URL and web app logins: `messages.requestUrlAuth`, `acceptUrlAuth`,
  `declineUrlAuth`, `checkUrlAuthMatchCode`, `messages.request*WebView`,
  `prolongWebView`, `sendWebViewData`, `bots.invokeWebViewCustomMethod`
  (a web app URL carries initData signed for this account);
- passkeys and push devices: `account.getPasskeys`,
  `initPasskeyRegistration`, `registerPasskey`, `deletePasskey`,
  `registerDevice`, `unregisterDevice`;
- business and bot grants: `account.updateConnectedBot`,
  `toggleConnectedBotPaused`, `disablePeerConnectedBot`,
  `getBotBusinessConnection`, `bots.allowSendMessage`,
  `toggleUserEmojiStatusPermission`, `messages.toggleBotInAttachMenu`
  (each hands a bot standing access to this account);
- `messages.requestEncryption`, `messages.sendPaidReaction`,
  `bots.exportBotToken`.

The regression suite checks every listed name against `api.tl`, so a typo
cannot silently refuse nothing.

Stars spent outside `payments.*` are refused by parameter name instead
(`HasPaidParam`): any method whose params carry `allow_paid_stars`,
`allow_paid_floodskip`, `suggested_post`, `send_paid_messages_stars`,
`paid_enabled` or `refund_charged` as a key, at any depth, fails with
`PermissionError`. `lux.messages.send` never approves Stars either, so the
server refuses it in a chat that charges for messages.

The Telegram service chat (user 777000) carries the login codes, so it is
never available to a plugin. `message`, `messageOut`, `messageEdited`,
`messageDeleted`, `chatRead` and the message menu skip it, `lux.chats.list`
leaves it out (`ChatVisible`), every `lux.messages` and `lux.chats` peer
argument resolves through `ResolvePeer`, which refuses it, and a message
from it carries `from: null`, as does a forward from it. A `mentionName`
entity of it is dropped both ways (`EntityJson`, `ParseEntities`).
`lux.account.me` never carries the phone. `api.call` refuses params that name it (`MentionsServiceUser`), and
results and `update` payloads drop every array element that mentions it,
including `updateServiceNotification` (`StripServiceUser`). A value that
still mentions it is refused whole, or not delivered at all. The account's
own phone number is removed from users marked `self` and from
`updateUserPhone` (`HideOwnPhone`).

The user id is matched however the TL encoder would read it: a number,
including a float such as `777000.0`, or a decimal string with ASCII
whitespace, a `+` or leading zeros (`IsServiceUserId`), under the keys
`user_id`, `peer`, `from_id`, `peer_id`, and `id` inside a user object, at
any depth. `lux.messages.send` refuses it as `peer`, and checks the peer it
resolved as well: a dialog id over 48 bits (`IsBareDialogId`), such as
2^48 + 777000, resolves to nothing instead of being masked into the
service chat.

The user approves the requested list at install. An update that requests
new permissions, or widens the `network` hosts, asks again before it is
applied.

Consent is bound to what the user saw. The install plan keeps the raw
`plugin.json` bytes read at prepare, the permissions shown in the dialog
and the hosts shown. At commit the manifest is read again and must equal
those bytes ("plugin.json changed since it was reviewed"). Every
permission or host new at commit must be among the shown ones, otherwise
the commit fails with "The plugin changed, review it again". The grant is
`(previously granted ∪ shown) ∩ requested`.

## GitHub source

Accepted input:

- `https://github.com/owner/repo`, `github.com/owner/repo`, `owner/repo`,
  optional `.git` and trailing slash: **auto** — latest Release, otherwise
  the default branch.
- `https://github.com/owner/repo/tree/<ref>[/<subdir>]`: branch or tag `ref`,
  plugin lives in `subdir`.
- `https://github.com/owner/repo/releases/tag/<tag>`: that release;
  `/releases/latest` is auto.
- `/blob/<ref>/<subdir>/<file>` (the file's folder), `/commit/<sha>`,
  `git@github.com:owner/repo.git`. A `tree/` ref may hold `/`: every split
  of `feature/x/plugin` into branch and folder is tried, shortest branch
  first.

Owner and repository names compare case-insensitively: one repository
folder can be installed once. The repository id and the commit are stored
with the source; a repository with the same name but a new id is refused
as an update.

Resolution (all HTTPS, `api.github.com`, `User-Agent: LuxuryGram`):

1. Auto: `GET /repos/o/r/releases/latest`. 200 → `channel=release`,
   `ref=tag_name`. 404 → `GET /repos/o/r` → `default_branch`, then
   `GET /repos/o/r/commits/<branch>` → `sha`; `channel=branch`, `ref=sha`,
   `label=branch`.
2. Files are fetched from
   `https://raw.githubusercontent.com/o/r/<ref>/<subdir/><path>`.
   Release tags and commit SHAs make installs reproducible.
3. Update check repeats step 1 with the stored channel (and stored branch
   label for branch channel). Update is available when the tag or SHA changed;
   an older release tag is a downgrade, not an update.
4. An auto link whose latest release has no `plugin.json` falls back to the
   default branch, and the plan says so in `InstallPlan::notice` (also
   written to the plugin log on install).

Rate limits and network errors are shown as text, never as a crash. A 403
with `X-RateLimit-Remaining: 0`, a 403 with `Retry-After` or a 429 reports
the time requests may resume. API answers are cached by `ETag` and asked
again with `If-None-Match`; a 304 does not count against the limit. A
non-JSON error body reads "GitHub: HTTP <status>". Every failure carries an
`ErrorCode` next to its text. `checkUpdatesDetailed` reports the available
and failed counts with the first error; a call while a check runs gets the
running check's result.

## Telegram API bridge (JSON form of TL)

The schema is `mtproto/scheme/api.tl`, embedded as a Qt resource and parsed
once at first use (`Tl::Schema::Parse`). JSON mapping:

| TL | JSON |
| --- | --- |
| object / method | `{"_": "messages.sendMessage", "peer": {...}, ...}` |
| `int`, `double` | number |
| `long` | string of decimal digits (numbers are also accepted on input) |
| `int128`, `int256` | hex string |
| `string` | string (UTF-8) |
| `bytes` | base64 string |
| `Bool` | `true` / `false` |
| `true` flag | `true` / absent |
| `Vector<T>` | array |
| optional `flags.N?T` | absent field = not set; flags are computed |

Encoding errors name the path (`peer._: unknown constructor "inputPeerX"`).
Responses are decoded to the same form; `gzip_packed` is unpacked.

## JS engine

QuickJS-ng (MIT), vendored at a fixed release into `luxury/libs/quickjs/`.
One runtime and context per plugin, on the main thread.

- Memory limit 64 MiB, stack 256 KiB per plugin.
- CPU: every entry into JS (load, event, callback, timer, promise job pump)
  has a 250 ms slice enforced by the interrupt handler. A rolling budget
  (`CpuBudget`) stops the plugin with "The plugin used too much CPU time"
  when its JS time over the last 5 s adds up to more than 500 ms (a
  rolling sum: pausing does not reset it), or it overruns the slice three
  times within 10 s. The manager then disables it. Entries are timed in
  microseconds of a monotonic clock and each costs at least 0.1 ms, so a
  flood of trivial entries is charged too. A drain of queued events pays
  that minimum once, on its first event; the rest pay their real time.
- Timers: at most 64 per plugin (`RangeError: too many timers`). An
  interval fires at most every 50 ms and a timeout waits at least 4 ms;
  smaller or invalid delays are raised to those.
- Events wait in one queue of at most 256, drained in one pass. When the
  plugin falls behind the oldest are dropped and the log says how many.
- At most 16 unhandled promise rejections are logged after each entry,
  then one "...and N more" line.
- When the active account changes or logs out, pending `fetch`, `api.call`
  and `messages.send` promises reject with `Error: the active account
  changed`, queued events are dropped and late replies are ignored.
- At most 16 `fetch`, `api.call` and `messages.send` requests in flight
  per plugin. The next
  one is rejected at once with `RangeError: too many requests in flight`.
- At most 64 `settings.onChange` handlers, all released on unload. A
  rejected handler of any kind is released too.
- A log line and `lastError` are cut to 4 KiB at a UTF-8 boundary.
  `lastError` is not written to `registry.json` on every change, and the
  manager's change signal is throttled to one per 250 ms.
- Plugin code is ES modules; `import` resolves only inside the plugin's
  `code/` directory. A top-level `await` in `main.js` may outlive the load:
  if it rejects later, the error goes to the entry that settles it and
  reaches the plugin's `lastError` like any other.
- No `std`/`os` modules, no file system, no eval of remote code beyond the
  plugin's own files.
- Host functions exchange JSON. A JS function passed as an argument reaches
  the host as `{"$fn": <id>}`; the host keeps it until it releases the id.
  Async host functions return Promises resolved by the host later.

## Plugin-facing API (`lux` global), version 1

```js
lux.plugin            // {id, version, previousVersion, reason, apiVersion,
//                       clientVersion, granted, lang}
//   granted: the permission ids the user approved; lang: the UI language id
lux.log(...args)      // also console.log/warn/error; shown in the manager
lux.on(event, fn)     // returns unsubscribe function
//   'message'      ({message})              messages.read
//   'messageOut'   ({message})              messages.read, after send
//   'messageEdited'  ({message})            messages.read
//   'messageDeleted' ({message})            messages.read, loaded messages
//   'chatRead'     ({peer, maxId, unread})  messages.read, inbox read
//   'accountChanged' ({})                   messages.read
//   'beforeSend'   ({peer, text, entities, replyTo})
//                  -> string|{text?, entities?, replyTo?}|false|undefined
//                  messages.modify; synchronous, a returned promise is
//                  ignored with a warning in the log
//   'update'       ({update})               telegram.updates, raw TL JSON,
//                                           at-least-once: may repeat
//   'chatOpened'   ({peer})                 ui, the main window's chat
//   'unload'       ()                       before disable/update/remove
//   any other name throws TypeError
lux.messages.send({peer, text, replyTo?, silent?})   -> Promise   messages.send
lux.messages.get(peer, id) -> message|null, or a Promise when not loaded  messages.read
lux.messages.history(peer, {limit?, offsetId?}) -> Promise<message[]>  messages.read
//   limit 1..100 (default 20), newest first
lux.messages.edit(peer, id, {text, entities?}) -> Promise          messages.manage
lux.messages.remove(peer, ids, {revoke?}) -> count                 messages.manage
//   the host function is messages.delete; ids: one id or 1..100
lux.messages.forward(peer, ids, to) -> count                       messages.manage
lux.messages.markRead(peer, maxId?)                                messages.manage
lux.messages.sendFile(peer, ArrayBuffer|view|path, {silent?})      messages.send
//   path: relative to the plugin's data folder (IsSafeRelativePath)
lux.messages.downloadMedia(peer, id) -> Promise<ArrayBuffer>       messages.read
//   photo or file, refused in chats that restrict saving, 60 s timeout
//   Files cross the JSON bridge as {$bin: base64}: at most 10 MiB
//   (kMaxMediaBytes, the client's in-memory download limit)
lux.chats.get(peer) -> {id, type, title, username, unreadCount}     messages.read
lux.chats.list({limit?}) -> chat[]   main list, 1..500, never 777000  messages.read
lux.account.me() -> {id, firstName, lastName, username, premium}    messages.read
lux.ui.toast(text)                     at most one a second (RangeError)  ui
lux.ui.notify(text)                    a toast titled with the plugin name  ui
lux.ui.alert({title, text}) / lux.ui.confirm({...}) -> Promise<bool>  ui
lux.ui.prompt({title, placeholder?, value?}) -> Promise<string|null>  ui
lux.ui.addCommand({name, description, handler({peer, args})})       ui
lux.ui.addMessageMenuItem({title, handler({peer, message})})        ui
lux.ui.addHotkey({keys: 'Ctrl+Shift+K', handler({peer})})           ui
//   hotkey peer: the chat open in the active window, or null
lux.ui.addChatMenuItem({title, handler({peer})})                    ui
lux.ui.addHeaderButton({title, handler({peer})})                    ui
lux.ui.addComposeMenuItem({title, handler({peer})})                 ui
//   100 each, the same title replaces the handler; removed on unload
lux.ui.showForm({title?, fields}) -> Promise<{key: value}|null>    ui
//   fields: 1..20 of {type: 'text'|'toggle'|'select', key, label?,
//   default?, placeholder? (text), options (select)}; an unknown type or
//   member throws TypeError; a dialog, see below
lux.ui.currentChat() -> peer|null / lux.ui.openChat(peer)           ui
lux.ui.openUrl(url)              http(s), one a second, within 1 s of a
//                               user gesture (NotAllowedError); a link
//                               TryConvertUrlToLocal changes (t.me, tg:,
//                               .ton) is a TypeError; the client's link
//                               confirmation box (HiddenUrlClickHandler)
lux.ui.decorateMessage({peer, id, text?}) / refreshMessage({peer, id})  ui
//   a label of at most 32 characters in the message badge, so shown only
//   on incoming messages with a sender name; at most 1000 messages;
//   dropped on unload and on an account switch
//   Dialogs: one at a time per plugin, titled with the plugin name, closed
//   when the plugin unloads; refused while the app is passcode-locked
//   Ctrl+C/V/X/A/Z/Y, Ctrl+Enter, Enter and Esc are reserved: TypeError
lux.http.fetch(url, {method, headers, body, binary}) -> Promise<{status, headers, text(), json(), base64}>  network
//   https only, to a manifest host, on port 443 unless that host entry
//   names a port ("api.example.com:8443")
//   binary: true returns the body as base64 instead of text
//   Every address of the host must be public: loopback, private, link-local,
//   CGNAT (100.64/10), ULA, multicast, 0.0.0.0, IPv4-mapped/NAT64 forms
//   of those and all of local-use NAT64 64:ff9b:1::/48 are refused with
//   PermissionError, for an IP literal and for each address the name
//   resolves to (QHostInfo; its 60 s cache is what the connection uses).
//   With an application proxy (SOCKS5/HTTP in the app settings) the proxy
//   resolves the name, so there is no local lookup that would leak it:
//   only an IP literal is checked. A Host header is dropped; a redirect to
//   another origin drops Authorization, Cookie, Proxy-* and Host.
lux.storage.get(key) / set(key, value) / remove(key) / keys()       storage
lux.settings.get(key) / set(key, value) / onChange(fn)   manifest settings, always allowed
//   set: a declared key and a value SettingAccepts, otherwise TypeError
lux.clipboard.read() / write(text)                                  clipboard
//   within 1 s after a command, menu item or hotkey of the plugin ran,
//   otherwise NotAllowedError
lux.api.call(method, params) -> Promise<result>                     telegram.api
setTimeout / clearTimeout / setInterval / clearInterval              always
```

A `message` object: `{id, peer, from, date, text, entities, out, replyTo,
hasMedia, media, forward, topicId, editDate, groupedId}` where `peer`/`from`
are `{id: "<decimal>", type: "user"|"chat"|"channel", title}`, `entities`
are `{type, offset, length, url?|userId?|emojiId?|language?}` in UTF-16
units, `media` is `{type, size}` or null and `forward` is
`{from, name, date, id}` or null. Ids are decimal strings.

`lux.messages.*` and `lux.chats.*` run in `Instance::data`
(`plugin_host_ext_data.cpp`); the permission table and argument parsing
are Qt-free in `plugin_payloads.h`.

Calling an API without its permission throws
`PermissionError: "<perm>" was not granted`.

## Client hooks

| Hook | Where | Behaviour |
| --- | --- | --- |
| incoming message | `Data::Session::addNewMessage`, `NewMessageType::Unread`, not outgoing | async dispatch on next event loop turn |
| raw updates | `Api::Updates::applyUpdates` (pushes and RPC results, once per seq batch: a gap queues it, a repeat is dropped), `differenceDone`, `channelDifferenceDone` | `update` event per update, at-least-once: a difference can repeat a pushed update, so dedupe by id/pts |
| outgoing text | `ApiWrap::sendMessage` entry | `beforeSend` chain, may rewrite or cancel; a cancel returns `false` and the chat and topic composers keep the text in the field |
| local command | `HistoryWidget::send` and `ChatWidget::send` (topics, replies) before `sendTextWithTags` | `/name args` consumed only when an enabled plugin registered `name`; a plugin command wins over a bot command of the same name |
| message menu | luxury context menu builder | one submenu "Plugins" with items |
| hotkey | app event filter | plugin hotkeys after built-in ones; never while a text field has focus, never a reserved shortcut |
| message edited | `Data::Session::updateEditedMessage` | `messageEdited` |
| message deleted | `Data::Session::processMessagesDeleted` and `processNonChannelMessagesDeleted`, loaded messages only | `messageDeleted` |
| chat read | `Api::Updates` on `updateReadHistoryInbox` and `updateReadChannelInbox` | `chatRead` |
| account switch | `Main::Domain::activeSessionChanges` | `accountChanged` |
| chat menu | `Window::FillDialogsEntryMenu` (chat list and the header's three-dot menu) | one submenu "Plugins" with `addChatMenuItem` items |
| header button | `HistoryView::TopBarWidget` | one button, shown while a plugin has `addHeaderButton` items, opens their menu |
| compose menu | message field context menu (`FieldAutocomplete` installs it once per field, for the field's own chat) | one submenu "Plugins" with `addComposeMenuItem` items |
| chat opened | `SessionController::activeChatChanges` of the main window, peer changed | `chatOpened` event on the next event loop turn |
| command list | `FieldAutocomplete`, `/` in a field | plugin commands listed with bot commands; choosing one puts it in the field, sending runs it |
| message label | `HistoryView::Message::refreshRightBadge` | reads the plugin's label cache; `decorateMessage` fills it and asks for a relayout |

`beforeSend` sees text sent through `ApiWrap::sendMessage` only: files,
albums, stickers, GIFs, inline results and forwards bypass it. Local
commands are read in the main chat composer only, not in topics or the
scheduled section.

Menu, button and command clicks reach the plugin through `callLater` on
the next event loop turn; layout and paint only read caches, so no hook
enters a plugin's engine from paint or layout. The service chat (777000)
and chats of another account get no plugin items, labels or events.

Hooks cost nothing when no plugin is enabled (single bool check).

## Python (phase 2, same host API)

- `runtime: "python"`: CPython compiled to WASI, run in WAMR. The `lux`
  module mirrors the JS API; permissions are enforced by the same host.
- `runtime: "python-native"`: separate trusted process, JSON-RPC over a pipe,
  full machine access, installed only after an explicit red warning.
