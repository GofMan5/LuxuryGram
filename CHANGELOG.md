# Changelog

This file tracks changes specific to LuxuryGram. Historical Telegram Desktop and inherited AyuGram changes remain available in [`changelog.txt`](changelog.txt) and the Git history.

The project has not published its first binary release.

## Unreleased

### Added

- Added a message context-menu action that translates any selected message to the language configured in LuxuryGram settings.

### Changed

- Synchronized the codebase with Telegram Desktop 7.0.9 while preserving LuxuryGram and AyuGram features.
- Published LuxuryGram forks for the updated `codegen`, `lib_ui`, `lib_tl`, and `lib_icu` submodules and restored Linux/Windows CI.
- Established the LuxuryGram project identity and public repository profile.
- Replaced inherited download and support links with verified LuxuryGram resources.
- Added contributor, conduct, security, support, legal, and pull-request guidance.
- Completed the LuxuryGram identity across executable names, desktop metadata, installers, bundle identifiers, crash UI, settings, and localization sources.
- Renamed inherited Ayu-prefixed application, language, codegen, and UI symbols while preserving compatibility-only storage paths, upstream services, and attribution.
- Renamed all project-owned `ayu` source, resource, QRC, style, and build paths to the canonical `luxury` namespace.

### Fixed

- Prevented missed media-download completions and main-thread observer teardown races in custom forwarding.
- Snapshotted forwarding inputs on the main thread and resolved messages by stable IDs so background work no longer retains UI-owned message/media pointers.
- Prevented fast-send completion races, overlapping album sends, unsafe download filename collisions, and cross-account forward-state collisions.
- Isolated hidden-message, filter, ghost-mode, message-history, and message-shot state between production, test, and multi-account sessions.
- Made settings and language-cache writes atomic, contained language cache/CDN paths, and bounded bulk message deletion memory.
- Made SQLite recovery preserve the database, WAL, and SHM as one rollback-safe set and stopped failed migrations from being marked successful.
- Keyed translation results by the complete text, entities, languages, and provider so edits and provider changes cannot reuse stale output.
- Removed dangling delayed UI/config callbacks and initialized inherited data/UI state deterministically.
- Bounded translator responses and batches, serialized message-history storage, and hardened network/media lifetimes.
- Removed dead UI overloads that broke warning-as-error Linux builds and eliminated redundant settings option copies.
