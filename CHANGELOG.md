# Changelog

This file tracks changes specific to LuxuryGram. Historical Telegram Desktop and inherited AyuGram changes remain available in [`changelog.txt`](changelog.txt) and the Git history.

Releases are published on the [Releases page](https://github.com/GofMan5/LuxuryGram/releases) and tagged `luxury-v<version>`.

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

