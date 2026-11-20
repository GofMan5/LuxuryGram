# Fork delta — classified, evidence-backed

**Method and honesty note.** The fork's merge base with `upstream/dev` is
`3a2a9a34e833d3243a13787215b8a7cdcf5dc3c3` (verified with `git merge-base HEAD
upstream/dev`; that commit is the upstream "Version 7.3.5." release). At the
Rust branch point `779e13d4c5`, `git diff --shortstat <base> 779e13d4c5`
reports **1178 files changed, 518659 insertions(+), 34339 deletions(-)**, matching
the preliminary inventory. At current `HEAD` (`65dc42c56e`) the same command
reports **1241 files changed, 527217 insertions(+), 34339 deletions(-)**; the
difference is exactly the three Rust-port commits (`71d4da5fbe`, `67e501f16c`,
`65dc42c56e`), which add 63 files under `rust/` and modify `AGENTS.md`.

The upstream checkout is **not** the behavioral reference. Fork behavior is
defined by this repository's sources and history (root `AGENTS.md`, user
instruction). Evidence below comes from `git show` on named commits,
`git diff <base> HEAD`, `git ls-files`, and reading the listed files. This is
**not an exhaustive classification of all 1178/1241 files**; see "Could not
verify" at the end.

## Classification legend

- **F — fork-only feature (port priority):** no upstream counterpart; all 220
  files under `Telegram/SourceFiles/luxury/` are additions (`A`) vs the base.
  These define LuxuryGram behavior and drive the port.
- **M — fork-modified upstream behavior:** upstream files/categories changed by
  the fork; fork behavior lives in hooks and overrides that a rewrite must keep.
- **C — compatibility-critical / upstream-synced:** wire protocol, crypto,
  storage format, signing/versioning and merged upstream code. Carry over
  through reviewed crates (ADR-004: `grammers-*`), never rewrite from memory.
- **G — generated / vendor:** third-party headers, submodules, Qt patches and
  test harnesses. Not product logic; do not port verbatim.

## F — fork-only features (port priority)

| Feature | Concrete paths | Evidence / port-priority rationale |
| --- | --- | --- |
| Plugin system (core, JS runtime, host API, install/consent/UI) | `Telegram/SourceFiles/luxury/plugins/` (32 files: `core/plugin_{manifest,permissions,version,source,registry}.{h,cpp}`, `core/tl_{codec,schema}.{h,cpp}`, `js/js_engine.{h,cpp}`, `plugin_{hooks,hooks_ui,host_api,host_ext_data,host_ext_ui,installer,manager,ui_logic,install_logic,host_logic,payloads}.{h,cpp}`), `docs/plugins/{ARCHITECTURE,AUTHORING}.md`, `docs/plugins/lux.d.ts`, `tools/tests/plugin_*_regressions.py` | All added vs base. Security/contract model is explicit in source: `plugin_permissions.h` (10 persisted permission bits, `IsMethodForbidden`, host allow-list matching), `plugin_manifest.h` (size caps 200 files / 20 MiB, safe-path rules), `plugin_host_api.h` (CPU budget, in-flight caps, network/API guards). Highest port priority: entirely fork-owned and behavior is pinned by offline regressions. |
| Chat analysis + draft refinement (newest active work) | `luxury/plugins/plugin_host_api.cpp`, `plugin_host_ext_data.cpp`, `plugin_hooks.cpp/.h`, `plugin_hooks_ui.cpp/.h`, `plugin_payloads.h`; `docs/plugins/examples/chat-analysis/{main.js,plugin.json,README.md}`; `tools/tests/chat_analysis_regressions.cjs` | Commits `98d9bd815d` → `e938cf6efa` → `779e13d4c5` are the chat-analysis series immediately before the Rust branch (`d45624e596`, between the first two, fixed plugin installer file handles). `779e13d4c5` touched `history_item.cpp`, top-bar widget, peer menu plus the host API with attribution/advice/scoped-export fixes. Port priority: most recently changed user-visible fork feature with a JS oracle. |
| WebSocket MTProto transport | `Telegram/SourceFiles/mtproto/details/mtproto_websocket_logic.{h,cpp}` (~836 lines), `mtproto_websocket_socket.{h,cpp}` (~458 lines), `mtproto/connection_tcp.{h,cpp}`, `mtproto/session{,private}.{h,cpp}`, `mtproto/mtp_instance.{h,cpp}`, `main/main_account.{h,cpp}`; setting in `luxury/luxury_settings.{h,cpp}` and `luxury/ui/settings/settings_other.cpp`; `docs/ws-transport.md`, `tools/tests/ws_transport_regressions.py` | Commits `4964248fba` (settings) and `61969fc4dc` (merge `net/wstransport`, 2853 insertions). `mtproto_websocket_logic.h` documents itself as the **Qt-free** part (endpoints, HTTP upgrade, RFC 6455 framing, route memory) compiled by a plain-g++ Python harness — a deterministic black-box seam. Note the fork-specific TLS/SNI behavior (mandatory SNI, pinned IP, no ignored SSL errors) is security-relevant. |
| Fork data storage and message/watch history | `luxury/data/{entities.h,luxury_database.{h,cpp},messages_storage.{h,cpp}}`; `luxury/features/watch/{presence_history.h,watched_media.{h,cpp}}`; `luxury/ui/watcher/*` | SQLite DB at `./tdata/ayudata.db` via vendored `sqlite_orm`; `entities.h` documents a 12 GB `DeletedMessage` table with 31.8M rows and an ordered async-write queue (`luxury_database.h:24-33`). This is the fork's own data model, not upstream `storage/local` — highest storage port priority. |
| Filters / forward / message shot / streamer mode / translator | `luxury/features/filters/*`, `features/forward/*`, `features/message_shot/*`, `features/streamer_mode/*` (per-OS backends), `features/translator/*` (Google `translate-pa.googleapis.com`, Yandex `translate.yandex.net` providers, `html_parser.*`) | All under the all-added `luxury/` tree. Network translators and per-OS streamer detection are fork-only behavior with external effects; port after the shared seams. |
| Ghost mode / no-forwards / profile change tracking | `luxury/luxury_state.{h,cpp}`, `luxury/utils/telegram_helpers.{h,cpp}`, hooks in `history_item.cpp`, `data_user.cpp`, `data_session.cpp` (see M) | Self-contained helpers plus hooks; `luxury_state.h` exposes `hide/isHidden` and story-close ghost-mode handling. |
| Custom settings, resources and themes | `luxury/luxury_settings.{h,cpp}` (756/2243 lines, `webSocketTransport` at `luxury_settings.h:51,374,474,667,691,786`), `luxury/ui/settings/*` (incl. `settings_luxury.cpp`, `settings_plugins.cpp`, `luxury_settings.style`), `luxury/ui/{luxury_styles.style,luxury_icons.style}`, `Telegram/Resources/art/luxury/*`, `Telegram/Resources/icons/luxury/*`, 567 added `luxury_*` keys in `Telegram/Resources/langs/lang.strings`; design tokens already generated in `rust/crates/design-tokens` (ADR-009, 51 scalars) | Fork UX surface. The three `luxury*.style` files are added; token extraction is the verified first GPUI-CE step. Composite style values are still a documented gap (`PROGRESS.md` → Known gaps). |

## M — fork-modified upstream behavior

| Area | Concrete paths | Evidence / port-priority rationale |
| --- | --- | --- |
| Update/apply pipeline hooks | `Telegram/SourceFiles/api/api_updates.cpp` (`Luxury::Plugins::UpdatesReceived` at lines 405/500/1543, `ChatRead`, `MessageSent`, `LuxuryOnline::noteProfileChange` at 2198/2206); `apiwrap.cpp:4937` (`BeforeSend`); `data/data_session.cpp` (`noteEdited`, `MessagesDeleted`, `IncomingMessage`, profile/gift hooks); `data/data_user.cpp:213-235`; `data/data_histories.cpp:1070`; `core/application.cpp` (`LuxuryInfra::init/finish`, plugin manager, hotkey) | These are the seams where fork behavior is grafted onto upstream flow. Commit `e51dc0b7d2` ("restore fork behavior lost in the 7.3.5 merge") proves an upstream sync can silently drop them — **the upstream checkout cannot be used as the behavioral reference**. |
| Chat list / message view / menus | `history/history_item.cpp` (`hasFromId`, `noteReactions`, `noteGift`), `history/history_inner_widget.cpp:3403`, `history/view/history_view_message.cpp:1234` (`MessageDecoration`), `history/view/history_view_chat_section.cpp`, `history/history_widget.cpp`, `history/view/history_view_top_bar_widget.cpp` (plugin header buttons), `window/window_peer_menu.cpp`, `window/window_controller.cpp`, `chat_helpers/field_autocomplete.cpp` (plugin commands/compose menu), `media/view/media_view_overlay_widget.cpp` (story-close ghost mode) | Fork-specific decoration, menus and commands live in upstream widgets. A Rust UI must preserve these entry points or reimplement their behavior. |
| Visual metric overrides | `Telegram/SourceFiles/{calls/calls.style,dialogs/dialogs.style,chat_helpers/chat_helpers.style,info/info.style,intro/intro.style,media/player/media_player.style,media/view/media_view.style,settings/settings.style,ui/chat/chat.style}` | `git diff` shows small numeric overrides (e.g. `dialogsOnlineBadgeStroke 2px→3px`, `Size 10px→12px`). These are fork changes to upstream UI behavior; feed them into design tokens, not raw constants. |
| Release/branding keys | `Telegram/SourceFiles/config.h` (`UpdatesPublicKey`), `Telegram/build/version`, `VERSIONING.md`, `CHANGELOG.md` | See C — these are fork-modified but security/compatibility-critical, so they are classified there. |

## C — compatibility-critical / upstream-synced

| Area | Concrete paths | Evidence / port-priority rationale |
| --- | --- | --- |
| Upstream v7.3.5 merge | Commit `5e1594c363` (parents `b0812134b3`, base `3a2a9a34e8`), 668 files / 59274 insertions; `Telegram/lib_*` submodule bumps, `Telegram/build/prepare/prepare.py`, `changelog.txt` | Upstream-synced by definition; do not treat upstream code as fork behavior. Carry protocol/schema changes forward through reviewed crates. |
| Submodule/vendored library pointers | `.gitmodules` repoints codegen/lib_ui/lib_tl/lib_spellcheck/lib_storage/lib_webview/cmake to `GofMan5/LuxuryGram-*` and adds `Telegram/lib_icu`; `:160000` pointer changes in the base→HEAD diff | Fork-patched upstream libraries. Compilation/link compatibility and any local patches must survive a Rust rewrite; they are not new features. |
| MTProto wire path (non-WebSocket) | `Telegram/SourceFiles/mtproto/{connection_tcp.cpp,session.cpp,session_private.cpp,mtp_instance.cpp}` and the TCP obfuscation/framing they contain | Per `rust/AGENTS.md` rule 5 and ADR-004: use `grammers-*`; never write MTProto/crypto from scratch. The fork's WS addition reuses the TCP framing, so compatibility with these bytes is mandatory. |
| Update signing / versioning / release tooling | `Telegram/SourceFiles/config.h`, `Telegram/build/{version,write_packer_private.py,write_packer_public.py,write_update_feed.py,check_release_mutability.py}`, `packaging/`, `.github/workflows/*` | Root `AGENTS.md`: RSA-4096 `UpdatesPublicKey` verification must not be weakened; `LuxuryUpdateVersion` gating and signed-feed flow must be preserved. |
| Build/compat maintenance commits | `51cfa32692` (dav1d MSVC 19.52 fix), `af2e579319` (Win phone-confirmation template), `e468cc05c3` (Linux CMake Python), `f5d32ae958`/`022b42fdb5` (`not_null` tests), `c619ba1ff8`/`927e792307` (release prep) | Upstream/toolchain compatibility work, not fork features. |

## G — generated / vendor (excluded from port)

| Area | Concrete paths | Evidence / rationale |
| --- | --- | --- |
| Vendored headers inside `luxury/` | `luxury/libs/quickjs/*` (14 headers: `quickjs.h`, `libregexp*`, `libunicode*`, …), `luxury/libs/sqlite/{sqlite3.h,sqlite_orm.h}`, `luxury/libs/json.hpp` (nlohmann) | Third-party source vendored under the fork tree; not fork logic. Replace with reviewed Rust crates or an FFI boundary, do not translate. |
| Third-party submodules and Qt patches | `Telegram/ThirdParty/*`, `Telegram/build/prepare/qtbase_6.11.2/*.patch`, `cmake/*` pointers | Vendored/external. Root `AGENTS.md` notes repo-local Qt patches are applied on Windows only. |
| Regression/test harnesses | `tools/tests/*` (161 files changed vs base, incl. `ws_transport_regressions.py`, `plugin_*_regressions.py`, `chat_analysis_regressions.cjs`, `ayugram_issue_ports_regressions.py`) | Fork-built verification infrastructure. Keep as oracles for black-box replay (ADR-002); not product code to port. The root-level untracked `ayugram_issue_ports_regressions.py` was **not touched**. |
| Generated localization keys | `Telegram/Resources/langs/lang.strings` block `// LuxuryGram keys generated` (567 added `luxury_*` keys) | Generated-style resource; phrase keys, not logic. Must keep keys stable when replacing `tr::` (MAPPING.md). |

## Could not verify (explicit gaps)

- **Not exhaustive.** I classified the named fork-owned areas and the recent
  commits, not all 1178/1241 changed files. Remaining top-level categories
  (`docs/` 18, `.github/` 45, `.agents/` 22, `.claude/` 12, `lib/` 6, and part
  of `tools/`) were not file-by-file classified.
- I did not open all 220 `luxury/` files; subarea classification is based on
  the full file listing plus representative headers (`luxury_state.h`,
  `luxury_infra.h`, `luxury_database.h`, `messages_storage.h`,
  `plugin_manifest.h`, `plugin_permissions.h`, `plugin_host_api.h`,
  `connection_tcp.h`, `mtproto_websocket_logic.h`) and the key commits.
- **Exact AyuGram base commit not located.** Many `luxury/` files carry the
  header "This is the source code of AyuGram for Desktop. Copyright @Radolyn,
  2026", while plugin files say "LuxuryGram for Desktop". Which features
  originated in AyuGram vs LuxuryGram was not resolved from local history.
- Custom `.style` tokens/themes are **not fully mapped** in `MAPPING.md`;
  only 51 scalar tokens are generated (ADR-009), composites remain a gap.
- No build, test, lint or runtime verification was run (per task scope), so
  no claim is made that any Rust code matches the C++ behavior described.
- WebSocket endpoint/SNI facts are **quoted from `docs/ws-transport.md`**
  (live-probed 2026-10-28 per that doc); I did not re-probe the network.
- Submodule *contents* were not inspected — only pointers and `.gitmodules`
  URLs. The contents of the 567 added localization keys and the plugin JS
  examples were not read in full.
- Because the upstream checkout is not the behavioral reference, "upstream
  counterpart" claims above rest on the base→HEAD diff shape (all `luxury/`
  files added) and commit messages, not on a separate upstream code comparison.

## Bottleneck candidates (hybrid plan: targeted replacement, not full port)

Plan per the lead/user: GPUI-CE for UI, Rust for MTProto/TL, LSM+Zstd and
Tantivy for data/search, media decoding **left in C/C++ through FFI (out of
scope)**. Candidate paths, ordered by likely payoff:

1. **Update processing / MTProto-TL** — `Telegram/SourceFiles/api/api_updates.cpp`
   (`UpdatesReceived`, `ChatRead`, `MessageSent`, `LuxuryOnline::noteProfileChange`
   hooks) and `data/data_session.cpp` (edit/delete/incoming hooks). Wire core:
   `mtproto/details/mtproto_websocket_logic.{h,cpp}` (Qt-free, deterministic
   Python-g++ harness), `mtproto/session_private.cpp`; fork TL helpers in
   `luxury/plugins/core/tl_{codec,schema}.{h,cpp}`. Rationale: every update
   crosses this path; the Qt-free WS core is the cleanest rust-anything
   black-box seam, but ADR-004 confirmation is required before adding a crate.
2. **Storage** — `luxury/data/{entities.h,luxury_database.cpp,messages_storage.cpp}`
   (`./tdata/ayudata.db`, sqlite_orm, ordered async queue, documented 12 GB /
   31.8M-row `DeletedMessage` table and costly `ALTER TABLE DROP COLUMN`
   migrations). Rationale: known scale and a measured startup-stall problem;
   LSM+Zstd candidate, but any replacement must preserve the on-disk schema,
   migration path and tdata location (`tdata/ayudata.db`, `-wal`).
3. **Search** — `luxury/data/luxury_database.cpp:771-806` and
   `messages_storage.cpp:234-242` (`searchQuery` substring read over the same
   large table; no FTS index in use), consumed by
   `luxury/ui/message_history/history_inner.cpp:819-848`. Rationale: clear
   Tantivy target; search behavior (case handling, caps) is pinned by the
   watcher UI and should be captured as an oracle before replacing the query.
4. **Message-list rendering (GPUI-CE)** — `luxury/ui/message_history/history_{inner,item,section}.{h,cpp}`,
   with upstream seams `history/history_inner_widget.cpp:3403`,
   `history/view/history_view_message.cpp:1234` (plugin decoration) and
   `luxury/ui/utils/{palette,color_utils}.{h,cpp}` plus the generated
   `rust/crates/design-tokens`. Rationale: the fork's biggest UI surface;
   retain decoration/search hooks and design tokens, do not port Qt widget
   structure (rust/AGENTS.md rule 1).

**Explicitly out of scope (hybrid plan):** media decoding — `Telegram/SourceFiles/media/`,
`Telegram/SourceFiles/ffmpeg/`, `media/streaming/`, `media/audio/` and the
libavcodec/libprisma/dav1d stack — stays C/C++ behind FFI. The recent album
cover commits (`4e00216cce`, touching `luxury/ui/boxes/expanded_player_box.cpp`
and `luxury/ui/utils/itunes_search.cpp`) are network fetch + UI concerns, not
decoding, and are therefore not pipeline candidates.
