# Writing a LuxuryGram plugin

A plugin is a GitHub repository (or a folder in one) with a `plugin.json`
and JavaScript ES modules. LuxuryGram downloads the listed files, shows the
permissions to the user and runs the code in a QuickJS sandbox with the
`lux` global.

Every number below comes from the client code; [ARCHITECTURE.md](ARCHITECTURE.md)
is the full contract.

## Quick start

1. Copy [examples/hello-world](examples/hello-world/) into a repository; it
   carries its own copy of [lux.d.ts](lux.d.ts).
2. Change `id` to something you own (`com.yourname.hello`).
3. Push, then in LuxuryGram open Settings → Plugins → Install from GitHub and paste
   `yourname/repo` (or the `tree/<branch>/<folder>` link of the folder).
4. Type `/hello` in any chat or press Ctrl+Shift+H.

The example uses the `select` and `number` setting types, so it installs on
the first LuxuryGram release after 1.0.21 and later ones; see
[New setting types](#new-setting-types).

For editor completion put this line at the top of each module and keep
[lux.d.ts](lux.d.ts) next to it:

```js
/// <reference path="./lux.d.ts" />
```

## plugin.json

```json
{
  "id": "com.yourname.hello",
  "name": "Hello",
  "version": "1.0.0",
  "description": "Greets you",
  "author": "yourname",
  "api": 1,
  "runtime": "js",
  "main": "main.js",
  "files": ["main.js", "lib/format.js"],
  "permissions": ["ui", "network"],
  "network": ["api.example.com", "*.example.org", "localhost:8443"],
  "minClient": "1.0.22",
  "homepage": "https://github.com/yourname/hello",
  "settings": [],
  "locales": {"ru": {"name": "Привет", "description": "Здоровается"}}
}
```

| Field | Rule |
| --- | --- |
| `id` | `^[a-z0-9][a-z0-9._-]{1,62}[a-z0-9]$`: 3-64 chars, no `..`, not a Windows device name (`con`, `nul`, `com1`, ...). It is the folder name and the key of the plugin's data. |
| `name`, `version`, `api`, `runtime`, `main` | Required. |
| `version`, `minClient` | Semver: `1.2.0`, optional `-beta.1` and `+build`; at most 9 digits per number, no leading zeros. |
| `api` | Plugin API major. This client supports `1`. |
| `runtime` | `"js"`. `"python"` and `"python-native"` are reserved and refused for now. |
| `main` | The entry module. |
| `files` | Every file to download, relative paths only, at most 200 files and 20 MiB in total. Must include `main`. Default `[main]`. |
| `permissions` | See [Permissions](#permissions). |
| `network` | Required with the `network` permission: hosts, `*.suffix` or `*` (any host, shown to the user as such), each with an optional `:port` (1-65535). |
| `minClient` | Older clients refuse the plugin: "This plugin needs LuxuryGram X or newer, this is Y." |
| `homepage` | An `http(s)` link. |
| `settings`, `locales` | See below. |

`plugin.json` itself must stay under 256 KiB. Unknown keys are ignored, so
a newer optional field does not break an older client. A wrong type is an
error, and so is a setting `type` the client does not know.

### Modules and builds

Only files listed in `files` are downloaded, and `import` resolves only
relative specifiers (`"./x.js"`, `"../x.js"`) inside the plugin. There is no
`node_modules` and no build step on the user's side: if you use TypeScript
or a bundler, commit the built JavaScript and list it in `files`.

## Settings

Each entry of `settings` becomes a row in the plugin's details box, and its
value is read with `lux.settings.get(key)`. Settings need no permission.

| Field | Rule |
| --- | --- |
| `key` | Required, unique, 1-64 of `[A-Za-z0-9_.-]`. |
| `type` | Required: one of the types below. |
| `title` | Required, the row label. |
| `description` | Optional string, validated; not shown yet. |
| `placeholder` | Optional string, validated; not shown yet (the field shows `title`). |
| `default` | Optional, must match the type. |
| `locales` | Optional, see [Locales](#locales). |

| Type | Value | Extra fields | Default without `default` |
| --- | --- | --- | --- |
| `string` | string | | `""` |
| `secret` | string, never written to the log | | `""` |
| `bool` | boolean | | `false` |
| `int` | integer | `min`, `max` (integers) | `min`, else `0` |
| `number` | finite number | `min`, `max`, `step` (positive, an input hint) | `min`, else `0` |
| `select` (alias `choice`) | one of `options` | `options`: non-empty array of distinct strings | the first option |
| `color` | `#RRGGBB`, either case | | `#000000` |

A `default` must fit: inside `min`..`max`, one of `options`, a valid color.
`min` must not exceed `max`.

`lux.settings.get` returns the stored value while the setting still accepts
it, otherwise the default. So when an update narrows a range or drops an
option, old values fall back to the default instead of reaching your code.

The settings box does not mask `secret` yet; treat it as "kept out of the
log", not as hidden on screen. Likewise `description`, `placeholder` and
`locales` are accepted and validated now and shown by the settings box in
a later release: today a row shows `title` only.

### New setting types

`select`, `number`, `color` and `secret` are new in the first release after
LuxuryGram 1.0.21. An older client refuses a manifest that uses them with
`settings[i].type: unknown type "select"`: settings are parsed before
`minClient` is compared, so `minClient` does not turn this into "update
LuxuryGram". Use `int`, `string` and `choice` if your plugin must install
on 1.0.21.

```js
lux.settings.onChange(({ key, value }) => { /* at most 64 handlers */ });
```

## Locales

`locales` maps a language code to translated texts, at the top level
(`name`, `description` of the plugin) and per setting (`name` for `title`,
plus `description`). They are accepted and validated now; the client shows
them in a later release, until then every text appears as written in the
manifest:

```json
"locales": {"ru": {"name": "Токен API", "description": "Со страницы настроек"}}
```

A language code is 2-16 of `[a-z0-9-]`, starts with a letter and does not
end with `-`: `ru`, `pt-br`. Both texts are optional strings.

## Manifest errors

Errors name the field path, so the install dialog points at the exact
place:

```text
id: must match ^[a-z0-9][a-z0-9._-]{1,62}[a-z0-9]$ without ".." or a device name, got "My_Plugin"
version: must look like 1.2.0
api: plugin needs API 2, this LuxuryGram supports up to 1; update LuxuryGram
runtime: "python" needs a newer LuxuryGram
files: must include main
files[3]: must be a safe relative path
network: is required with the network permission
network[0]: must be a host, *.suffix or *, with an optional :port
homepage: must be an http(s) link
settings[2].default: must be one of options
settings[0].locales.RU: must be a language code like "ru" or "pt-br"
plugin.json: is not valid JSON
plugin.json: is larger than 256 KiB
```

## Install links and updates

Any of these can be pasted into the install box:

| Link | Source |
| --- | --- |
| `owner/repo`, `github.com/owner/repo`, `https://github.com/owner/repo` (optional `.git`, trailing `/`) | The latest Release; without releases, the default branch. |
| `https://github.com/owner/repo/tree/<ref>/<subdir>` | Branch or tag `ref`, plugin in `subdir`. A branch with `/` in its name is written with `%2F`. |
| `https://github.com/owner/repo/releases/tag/<tag>` | That release. |

Files are fetched from `raw.githubusercontent.com` at the release tag or the
branch commit, so an install is reproducible. The update check asks the same
channel again: a new release tag, or a new commit on the branch, is an
update. Publish Releases if you want users to get only tested versions.

The download times out after 30 s; a GitHub API answer over 1 MiB is
refused. Rate limits show as text in the dialog.

An update that asks for a new permission or widens `network` shows the
consent dialog again before it is applied. The grant covers only what the
user saw: if `plugin.json` changes between the dialog and the commit, the
install fails with "The plugin changed, review it again".

## Permissions

| Id | Grants | Dangerous |
| --- | --- | --- |
| `messages.read` | `message` and `messageOut` events; full messages in menu handlers | |
| `messages.send` | `lux.messages.send` | yes |
| `messages.modify` | the `beforeSend` event: rewrite or cancel outgoing text | yes |
| `ui` | `lux.ui`: toasts, dialogs, commands, message menu items, hotkeys | |
| `network` | `lux.http.fetch` to the `network` hosts | |
| `storage` | `lux.storage` | |
| `clipboard` | `lux.clipboard` | |
| `telegram.api` | `lux.api.call`, raw MTProto methods | yes |
| `telegram.updates` | the `update` event, the raw update stream | yes |

Dangerous permissions are highlighted in the consent dialog. Ask only for
what you use. Calling an API without its permission throws
`PermissionError: "<perm>" was not granted`.

| Event | Permission |
| --- | --- |
| `message` | `messages.read` |
| `messageOut` | `messages.read` |
| `beforeSend` | `messages.modify` |
| `update` | `telegram.updates` |
| `unload` | none |

What is never available, whatever is granted:

- The Telegram service chat (user 777000), which carries login codes: it is
  skipped in events and menus, refused in `api.call` params and removed
  from results and updates.
- The account's own phone number: removed from the `self` user and from
  `updateUserPhone`.
- MTProto methods for login, sessions, password, Passport, payments and
  Stars, web app logins and bot grants (`auth.*`, `payments.*` writes,
  `account.getAuthorizations`, ...), and any call whose params spend Stars.
  The full list is in [ARCHITECTURE.md](ARCHITECTURE.md#permissions).

## Limits

| What | Limit |
| --- | --- |
| Memory | 64 MiB per plugin |
| Stack | 256 KiB |
| One entry into JS (load, event, timer, callback) | 250 ms |
| CPU over any 5 s | 500 ms; or 3 overruns of the 250 ms slice within 10 s. Either stops and disables the plugin: "The plugin used too much CPU time" |
| Timers | 1000 per plugin; an interval at least 10 ms, a timeout at least 4 ms |
| Requests in flight (`fetch`, `api.call`, `messages.send`) | 16; the next one rejects with `RangeError` |
| Commands, message menu items, hotkeys | 100 each; the same name, title or keys replaces the handler |
| `settings.onChange` handlers | 64 |
| Log line, `lastError` | 4 KiB |
| Storage | 5 MiB of JSON per plugin, written to disk 1 s after the last change |
| `fetch` | HTTPS only, 30 s timeout, 10 MiB response, 5 redirects (each checked against `network`), 32 MiB after decompression |
| Install | 200 files, 20 MiB, `plugin.json` 256 KiB |

Keep handlers short and move slow work to `async` calls: the 250 ms slice
counts only JavaScript time, not time spent waiting on a Promise.

## Errors

Host calls throw, or reject their Promise, with an `Error` whose `name` is:

| Name | When |
| --- | --- |
| `PermissionError` | the permission is not granted, or the call is never allowed |
| `TypeError` | a wrong argument (bad key, empty text, reserved hotkey) |
| `RangeError` | a limit: handlers, timers, requests in flight, response size |
| `QuotaExceededError` | storage over 5 MiB |
| `NetworkError` | a failed request or too many redirects |
| `Error` | no active account or window, or the plugin is stopping |

A Telegram API error rejects with `name` set to the error type
(`FLOOD_WAIT_X`, `PEER_ID_INVALID`) and `message` set to
`{"type":"...","code":420}`.

An uncaught error in a handler is written to the plugin's log and shown as
its last error in the manager; the plugin keeps running. An error while
`main` loads leaves the plugin stopped with that error.

## Lifecycle

The plugin loads when it is enabled and when LuxuryGram starts. Before it is
disabled, updated or removed it gets `unload`; every handler, timer and
pending request is released after that. Store anything you need across
restarts with `lux.storage` or read it from `lux.settings`.

## See also

- [lux.d.ts](lux.d.ts): the API with comments.
- [examples/hello-world](examples/hello-world/): a complete plugin.
- [ARCHITECTURE.md](ARCHITECTURE.md): the host, the sandbox and the
  security rules in full.
