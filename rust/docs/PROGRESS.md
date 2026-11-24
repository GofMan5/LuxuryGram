# Port progress

Branch: `luxurygram-rust` (created from `dev` at `779e13d4c5`).
Goal: `goal-18aca4b2-a37a-4279-bd0a-bb61496d2351` (hybrid bottleneck plan per ADR-010, 100 rounds).

## Milestones (hybrid plan, ADR-010)

| ID | Scope | Status |
| --- | --- | --- |
| H0 | Bottleneck inventory + baseline measurements | **done** — fork delta (`FORK_DELTA.md`), GPUI API map (`MAPPING.md`), H3 storage options (`research/storage-options.md`), and H1 scroll baseline recorded |
| H1 | UI slice: virtualized message list on GPUI-CE | **in progress** — `message.rs` + `feed.rs` landed: bottom-aligned `ListState` with tail following, date separators, sender grouping, entity-styled runs, Ctrl+F search with live filtering, and 4 passing tests; release bench wired and run; found and fixed a whole-vector clone in render (27 ms → 0.15 ms/frame) |
| H2 | MTProto/TL slice: grammers + tokio off the main thread | blocked on ADR-004 confirmation |
| H3 | Storage/search slice: LSM + Zstd and Tantivy | pre-ADR research done (`research/storage-options.md`); synthetic 1M/5M benchmark + ADR next |
| H4 | Media cache: SHA-256 CAS + WebP/AVIF previews | not started |
| Out of scope | Rewriting FFmpeg, WebRTC, dav1d, rlottie | stays C/C++ behind FFI |
| Kept from old plan | M0 GPUI shell, design tokens, zxcvbn_translit oracle | done and still valid |

## Done so far (still valid under the hybrid plan)

- `cargo install rust-anything` → 2.1.0, `rust-anything setup` registered MCP/skill
  for the five supported agents; MCP `initialize` smoke test passed.
- Created the `luxurygram-rust` branch without touching pre-existing work.
- Created the Cargo workspace under `rust/` and ported the standalone
  `zxcvbn_translit` build tool (C++ CLI) to pure Rust:
  `rust/crates/zxcvbn-translit/{src/lib.rs,src/main.rs}`.
- Recorded 7 black-box cases from the original C++ binary with
  `rust-anything app record`; replay against the original: **7/7**; against the
  Rust release binary: **7/7** after fixing one real bug found by the replay
  (U+0400–U+040F lowercase shift was `+0x20` instead of `+0x50`, breaking `Ё`).
- Added `AGENTS.md` rule 7: intermediate commits and pushes are mandatory.
- Added the M0 GPUI-CE shell (`rust/apps/luxurygram-shell`) pinned to
  `gpui-ce = 0.3.0` and `gpui_ce_platform = 0.3.0`. The published
  "Start a project" guide still says `0.2`/`0.1`; exact versions were verified
  against crates.io metadata and the downloaded crate's own `examples/` and
  `src/platform.rs` before use.
- Generated 51 scalar design tokens from the fork's three `luxury*.style`
  files into `rust/crates/design-tokens/src/lib.rs` using
  `rust/tools/generate-tokens.ps1`; each token records its source file and line.
- **Visual check (2026-11-20):** launched `target/debug/luxurygram-shell.exe`,
  it stayed alive for 5 s with no stdout/stderr, and a desktop screenshot
  showed the "LuxuryGram" title bar and centered M0 text. Cropped evidence:
  `rust/docs/assets/m0-shell.png`. Process was stopped afterwards.
- User replaced the full-rewrite objective with the hybrid bottleneck plan
  (ADR-010); no full-app rewrite claim is made.
- Completed H0 inventory & research artifacts:
  - `rust/docs/FORK_DELTA.md` — fork delta vs `upstream/dev` (`3a2a9a34e833d3243a13787215b8a7cdcf5dc3c3`) and bottleneck inventory (MTProto/WebSocket, `ayudata.db`, unindexed search, message list).
  - `rust/docs/MAPPING.md` — verified C++/Qt → GPUI-CE `0.3.0` API mapping against pinned Cargo registry sources (`gpui-ce-0.3.0`, `gpui_ce_platform-0.3.0`).
  - `rust/docs/research/storage-options.md` — verified crate comparison (`fjall 3.1.13` + `zstd 0.14.4`, `rocksdb 0.25.0`, `tantivy 0.27.0`, `surrealkv 1.4.0` rejected) and 1M/5M synthetic benchmark plan.
- Added and ran H1 UI scroll baseline benchmark (`rust/apps/luxurygram-shell/benches/message_list_scroll.rs`, gated behind `bench-support` feature) using `#[gpui::bench(fps = 120)]` + `BenchAppContext::bench_renderer` over `gpui::list()` (`ListAlignment::Bottom`, `ListState::scroll_by`) with synthetic message bubbles, `StyledText` highlights, and `design_tokens::LUXURY_WATCHER_CARD_RADIUS`.

## Known gaps

- The GPUI shell is a tested standalone binary, not the production `Telegram` target. The active desktop application still starts `Telegram/SourceFiles/main.cpp` and uses Qt; full GPUI replacement remains a separate migration.
- Run `cargo run -p luxurygram-shell -- --history-json <result.json>` from `rust/` to preview a single-chat JSON export (up to 16 MiB) without starting Qt or logging in. This uses the root format from `Telegram/SourceFiles/export/output/export_output_json.cpp:2956–3000`; no arguments opens the synthetic demo.
- The preview uses the export’s `from`, `date_unixtime` and `text`, not a live chat. It shows text and captions without entity styling, does not infer outgoing direction, and skips service, rich-document and media-only rows (`export_output_json.cpp:1534–1567, 1618–1631, 2125–2337`). Dates/times are UTC-based demo labels, not the Qt export’s local ISO dates.
- The existing `HistoryInner` and `ListWidget` paths remain Qt (`Telegram/SourceFiles/history/history_inner_widget.cpp`, `Telegram/SourceFiles/history/view/history_view_list_widget.cpp`). The preview has no live updates, pagination, media playback, selection, accessibility parity, or context menus. The main window, dialogs, compose, settings, authentication, calls and platform integration are unchanged.

## Checks actually run

- `cargo bench -p luxurygram-shell --features bench-support --bench message_list_scroll` — exit 0 (2026-11-21, Windows x64, `BenchAppContext` headless without GPU present; `current_headless_renderer()` returns `Ok(None)` on Windows so measurements cover layout, `ParleyTextSystem` shaping, prepaint, and scene build):
  - `10000-msgs-step-48px`: Criterion median **1.3150 ms** (`[1.2957 ms .. 1.3400 ms]`); `window dirty-to-draw` mean **1.320 ms** (p50 **1.264 ms**, p95 **1.580 ms**, p99 **1.846 ms**, max **2.365 ms**, 0 budget overruns at 120 FPS / 3333 samples); `2.555 K` allocs / `1.155 M` bytes per iter.
  - `100000-msgs-step-48px`: Criterion median **1.3341 ms** (`[1.2997 ms .. 1.3914 ms]`); `window dirty-to-draw` mean **1.319 ms** (p50 **1.266 ms**, p95 **1.573 ms**, p99 **1.931 ms**, max **11.289 ms**, 1 overrun); `2.557 K` allocs / `1.157 M` bytes per iter.
  - `100000-msgs-step-480px`: Criterion median **1.4107 ms** (`[1.3353 ms .. 1.4957 ms]`); `window dirty-to-draw` mean **1.361 ms** (p50 **1.278 ms**, p95 **1.725 ms**, p99 **2.402 ms**, max **4.772 ms**, 0 overruns); `2.557 K` allocs / `1.157 M` bytes per iter.

- `cargo test --workspace` — 7 passed, 0 failed (1 design-tokens + 6 zxcvbn-translit).
- `cargo clippy --workspace --all-targets -- -D warnings` — exit 0 (final full run).
- `cargo build --release` — exit 0.
- Offline workspace check, Clippy with warnings denied, workspace tests (16 passed), and Debug shell build passed. On Windows, a BOM-free fixture opened a native window titled `LuxuryGram — Read-only chat export`, closed normally, and retained its input hash. Account-archive shape was rejected. This smoke test did not inspect pixels or use an account.
- `rust-anything app record rust --inputs … --no-traps -- …` — 7 cases recorded.
- `rust-anything app replay rust` — 7/7 (original).
- `rust-anything app replay rust --build "target/release/zxcvbn-translit.exe"`
  — 7/7 (Rust).
- The full-workspace `cargo test`/`cargo clippy` re-run after adding GPUI deps
  was interrupted once by the user, then completed successfully after the game:
  `cargo test --workspace` 7/7 and `cargo clippy --workspace --all-targets --
  -D warnings` exit 0.

## Known gaps

- **H1 baseline is CPU/scene-build only on Windows (`gpui-ce = 0.3.0`).** `gpui_ce_platform::current_headless_renderer()` returns `Ok(None)` on Windows (`gpui_ce_platform-0.3.0/src/gpui_platform.rs:239-242`), so `message_list_scroll` excludes DirectX/wgpu GPU atlas upload and present cost; windowed GPU frame-time verification is still needed in H1.
- **M0 shell visual check (`rust/docs/assets/m0-shell.png`)** verified the window launches and renders the M0 placeholder, but no UI parity with Qt (`history_inner` / `history_item` / `history_section`) exists yet.
- **rust-anything cannot parse C++/Qt.** Only black-box CLI recording and a future flat C ABI seam are usable. Everything ported so far uses the black-box path; UI equivalence needs separate visual tests.
- `rust/verification/zxcvbn-translit/reference/` holds a locally built C++ reference binary and is gitignored; rebuild with `build-reference.ps1`.
- Only 7 hand-written `zxcvbn_translit` cases; no fuzzing or real dictionary corpus yet.
- Design tokens (`rust/crates/design-tokens/src/lib.rs`) only cover 51 scalar tokens from the three `luxury*.style` files; composite style values (`margins()`, `point()`, `icon()`, colors) and upstream `.style` files are not extracted yet.
- The GPUI dependency tree (plus `bench-support` dev/bench deps) adds heavy first-build compile time (~6m56s for release bench build); CI/fresh-machine cost has not been measured.

## fast-anything trial (2026-11-23, user-requested tool)

- Installed fast-anything 0.8.0 (npm; the local crates.io index does not have
  `fast-anything`), `doctor` ready. Full evidence: `docs/fast-anything-trial.md`.
- Profiled the real C++ transliterator on a 2.46 MB English dictionary and a
  2.26 MB Cyrillic workload. `TransformDictionary` is 95.3% / 98.6% of the run.
- Refactored `zxcvbn_translit.cpp` into pure, portable functions with **no
  behavior change**: output SHA-256 matches the pre-existing reference binary on
  both workloads.
- Wrote and verified two Rust ports: `MapWord` 786/786 calls matched,
  `TransformDictionary` 35/35 and 36/36 matched. Per-call: 20× slower for
  `MapWord`; 1.2× faster for `TransformDictionary` on Cyrillic input.
- `fast-anything bench --runs 5` measured **250 ms C++ vs 299 ms Rust end to end**
  (0.8×): the generated C-bridge JSON-copies the whole dictionary. The verified
  port was disabled; the source has no fast-anything markers and the two output
  hashes were re-checked after disabling. No runtime behavior changed.
- Selection rule recorded: only port functions whose CPU work dominates their
  JSON bridge cost; do not enable anything that loses `fast-anything bench`.

## Binary FFI follow-up (same checkpoint)

- fast-anything's JSON bridge was the only reason the verified port lost end to
  end. Replaced it with a binary C ABI (`ffi` feature on `zxcvbn-translit`) and
  an opt-in call site in `zxcvbn_translit.cpp`; default behavior stays C++.
- `transform_optimized` removes a per-word `String` allocation. In-process,
  20 interleaved iterations: 8.038 ms → 4.521 ms (1.78×) on the 2.46 MB English
  dictionary and 31.250 ms → 12.403 ms (2.52×) on the 2.26 MB Cyrillic one.
- Output is byte-identical to the reference binary on both dictionaries and all
  7 recorded oracle cases. `cargo fmt --all`, both clippy runs with `-D warnings`
  and `cargo test --workspace` pass. Details in `docs/fast-anything-trial.md`.

## H1 first slice (2026-11-23)

- Added `apps/luxurygram-shell/src/message.rs`: deterministic message model
  (sender, text, byte-range entity spans, timestamp), date separators by day and
  15-minute consecutive-sender grouping, mirroring `recountDisplayDateInBlocks`
  and `computeIsAttachToPrevious`.
- Added `apps/luxurygram-shell/src/feed.rs`: `list()` with
  `ListAlignment::Bottom` + `FollowMode::Tail`, variable-height bubbles, date
  headers, `StyledText` highlights for bold/italic/link/code entities, and
  `scroll_to_end`. Wired as the shell's main view.
- `cargo check --workspace --all-targets`, `cargo clippy --workspace
  --all-targets -- -D warnings` and `cargo test -p luxurygram-shell` pass;
  tests prove only visible rows are rendered out of 10,000 and that tail
  following is restored after a scroll up + `scroll_to_bottom`.
- Search bar added: Ctrl+F opens a core-GPUI focused field (no external
  `gpui_ce_elements` dependency), printable keys and Backspace update the query,
  Escape closes it, and the list is filtered/reset in place. `End` scrolls to
  bottom. Four `TestApp` tests now cover virtualization, tail following,
  query filtering and key handling.
- The shell is now a lib + bin so benchmarks measure the shipped view.
  `message_list_scroll` gained `scroll_feed_production` using the real
  `feed::MessageFeed`, alongside the H0 baseline `scroll_feed`.
- First bench run exposed a real bug in the new view: `render()` cloned the
  whole 100k-message vector every frame (240K allocations, 27 ms/frame). Fixed
  by holding `Rc<Vec<Message>>` and cloning only the `Rc`. Also corrected the
  bench to count rows per frame instead of cumulatively.
- Release bench after the fix (`cargo bench -p luxurygram-shell --features
  bench-support --bench message_list_scroll`, 20 samples per case):
  `message_list_scroll_production` 10k/48px **152 µs**, 100k/48px **157 µs**,
  100k/480px **424 µs**, versus 27.3 ms / 27.3 ms / 27.4 ms before the fix
  (−93.9% / −99.4% / −98.3%). H0 baseline `message_list_scroll` stayed at
  1.34 / 1.37 / 1.33 ms. The two groups use different row content, so treat
  their absolute values as separate series, not a strict A/B.

## H3 baseline measured (2026-11-23)

Disposable harness `.ai/h3-bench/` (outside the workspace, gitignored) reproduces
the exact C++ query shapes from `luxury_database.cpp`: WAL `deleted_message`
with the composite index, the `luxury_contains_ignoring_case` scalar UDF and
`ORDER BY message_id DESC LIMIT 50`. Deterministic 200-dialog mixed
ASCII/Cyrillic payloads; `rusqlite 0.41.1` bundled.

| rows | ingest | db size | indexed read p50/p99 | substring search p50/p99 |
| --- | --- | --- | --- | --- |
| 1,000,000 | 10.794 s (92.6k rows/s) | 533.9 MiB | 28.8 / 65.9 ms | 59.8 / 79.7 ms |
| 5,000,000 | 69.656 s (71.8k rows/s) | 2675.7 MiB | 166.2 / 371.3 ms | 303.3 / 382.0 ms |

These are warm-cache numbers on Windows x64; they already miss the ADR-010
targets (read p99 < 5 ms, search p99 < 20 ms) by 74× for reads and 19× for
search at 5M rows. The harness is not committed; only this evidence is. No
storage/search dependency has been added to the workspace — the Fjall vs
RocksDB vs Tantivy decision still needs candidate runs and an ADR.

## Next checkpoint (hybrid plan)

1. Begin H1: build the virtualized chat message feed view in `luxurygram-shell` (extracting shared bubble/message model from the benchmark, adding entity-styled runs, message reactions/headers, and live scroll controls) and visually verify in a real window.
2. Run the standalone 1M/5M synthetic storage/search benchmark (`research/storage-options.md`) and write the H3 ADR (`fjall` + `zstd` vs `rocksdb` + `tantivy`) before adding any storage crate.
3. Confirm ADR-004 (`grammers-*` + `tokio`) with the user before starting H2.

## Blocked

- ADR-004 awaits user confirmation before adding MTProto dependencies.
