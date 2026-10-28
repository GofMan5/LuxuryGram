# LuxuryGram plugin system — architecture contract

Status: phase 1 (JavaScript). Phase 2 adds Python on the same host API.

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
  plugins/<plugin-id>/
    code/                    downloaded files, read-only for the plugin
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
    {"key": "mode", "type": "choice", "title": "Mode", "default": "all", "options": ["all", "contacts"]}
  ],
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
- Unknown keys are ignored (forward compatible). Wrong types are errors.

## Permissions

| Id | Grants | Dangerous |
| --- | --- | --- |
| `messages.read` | every incoming message of the account, in every chat, plus the message passed to menu actions | |
| `messages.send` | `lux.messages.send` | yes |
| `messages.modify` | rewrite or cancel outgoing text | yes |
| `ui` | commands, menu items, hotkeys, toasts, dialogs | |
| `network` | `lux.http.fetch` to manifest hosts only | |
| `storage` | `lux.storage` | |
| `clipboard` | read and write the clipboard | |
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
never available to a plugin. `message`, `messageOut` and the message menu
skip it. `api.call` refuses params that name it (`MentionsServiceUser`), and
results and `update` payloads drop every array element that mentions it,
including `updateServiceNotification` (`StripServiceUser`). A value that
still mentions it is refused whole, or not delivered at all. The account's
own phone number is removed from users marked `self` and from
`updateUserPhone` (`HideOwnPhone`).

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
- `https://github.com/owner/repo/releases/tag/<tag>`: that release.

Resolution (all HTTPS, `api.github.com`, `User-Agent: LuxuryGram`):

1. Auto: `GET /repos/o/r/releases/latest`. 200 → `channel=release`,
   `ref=tag_name`. 404 → `GET /repos/o/r` → `default_branch`, then
   `GET /repos/o/r/commits/<branch>` → `sha`; `channel=branch`, `ref=sha`,
   `label=branch`.
2. Files are fetched from
   `https://raw.githubusercontent.com/o/r/<ref>/<subdir/><path>`.
   Release tags and commit SHAs make installs reproducible.
3. Update check repeats step 1 with the stored channel (and stored branch
   label for branch channel). Update is available when the tag or SHA changed.

Rate limits and network errors are shown as text, never as a crash.

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
  times within 10 s. The manager then disables it.
- Timers: an interval fires at most every 10 ms and a timeout waits at
  least 4 ms; smaller or invalid delays are raised to those.
- At most 16 `fetch`, `api.call` and `messages.send` requests in flight
  per plugin. The next
  one is rejected at once with `RangeError: too many requests in flight`.
- At most 64 `settings.onChange` handlers, all released on unload. A
  rejected handler of any kind is released too.
- A log line and `lastError` are cut to 4 KiB at a UTF-8 boundary.
  `lastError` is not written to `registry.json` on every change, and the
  manager's change signal is throttled to one per 250 ms.
- Plugin code is ES modules; `import` resolves only inside the plugin's
  `code/` directory.
- No `std`/`os` modules, no file system, no eval of remote code beyond the
  plugin's own files.
- Host functions exchange JSON. A JS function passed as an argument reaches
  the host as `{"$fn": <id>}`; the host keeps it until it releases the id.
  Async host functions return Promises resolved by the host later.

## Plugin-facing API (`lux` global), version 1

```js
lux.plugin            // {id, version, apiVersion, clientVersion}
lux.log(...args)      // also console.log/warn/error; shown in the manager
lux.on(event, fn)     // returns unsubscribe function
//   'message'      ({message})              messages.read
//   'messageOut'   ({message})              messages.read, after send
//   'beforeSend'   ({peer, text}) -> string|false|undefined   messages.modify
//   'update'       ({update})               telegram.updates, raw TL JSON,
//                                           at-least-once: may repeat
//   'unload'       ()                       before disable/update/remove
lux.messages.send({peer, text, replyTo?, silent?})   -> Promise   messages.send
lux.ui.toast(text)                                                 ui
lux.ui.alert({title, text}) / lux.ui.confirm({...}) -> Promise<bool>  ui
lux.ui.prompt({title, placeholder?, value?}) -> Promise<string|null>  ui
lux.ui.addCommand({name, description, handler({peer, args})})       ui
lux.ui.addMessageMenuItem({title, handler({message})})              ui
lux.ui.addHotkey({keys: 'Ctrl+Shift+K', handler()})                 ui
//   Ctrl+C/V/X/A/Z/Y, Ctrl+Enter, Enter and Esc are reserved: TypeError
lux.http.fetch(url, {method, headers, body}) -> Promise<{status, headers, text(), json()}>  network
//   https only, to a manifest host, on port 443 unless that host entry
//   names a port ("api.example.com:8443")
lux.storage.get(key) / set(key, value) / remove(key) / keys()       storage
lux.settings.get(key) / onChange(fn)          values from manifest settings, always allowed
lux.clipboard.read() / write(text)                                  clipboard
lux.api.call(method, params) -> Promise<result>                     telegram.api
setTimeout / clearTimeout / setInterval / clearInterval              always
```

A `message` object: `{id, peer, from, date, text, out, replyTo, hasMedia}`
where `peer`/`from` are `{id: "<decimal>", type: "user"|"chat"|"channel", title}`.

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

`beforeSend` sees text sent through `ApiWrap::sendMessage` only: files,
albums, stickers, GIFs, inline results and forwards bypass it. Local
commands are read in the main chat composer only, not in topics or the
scheduled section.

Hooks cost nothing when no plugin is enabled (single bool check).

## Python (phase 2, same host API)

- `runtime: "python"`: CPython compiled to WASI, run in WAMR. The `lux`
  module mirrors the JS API; permissions are enforced by the same host.
- `runtime: "python-native"`: separate trusted process, JSON-RPC over a pipe,
  full machine access, installed only after an explicit red warning.
