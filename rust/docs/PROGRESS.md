# Port progress

Branch: `luxurygram-rust` (created from `dev` at `779e13d4c5`).
Goal: `goal-ee1acc77-35bc-46e8-9b6a-72fc9c3045f5` (full Rust rewrite, 100 rounds).

## Milestones

| ID | Scope | Status |
| --- | --- | --- |
| M0 | Workspace, build/CI discipline, first behavior-proven port, GPUI shell | **in progress** — first slice verified; shell compiles; tokens generated |
| M1 | TL + MTProto + login via a reviewed crate | not started (ADR-004 needs confirmation) |
| M2 | Data model + local storage | not started |
| M3 | Dialogs list on GPUI-CE | not started |
| M4 | Message history | not started |
| M5 | Composer + media | not started |
| M6 | Stickers/emoji/viewer | not started |
| M7 | Settings/folders/search/notifications | not started |
| Out of scope for now | Calls, secret chats | explicitly deferred |

## Done in M0 (2026-11-20)

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

## Checks actually run

- `cargo test --workspace` — 7 passed, 0 failed (1 design-tokens + 6 zxcvbn-translit).
- `cargo clippy --workspace --all-targets -- -D warnings` — exit 0 (final full run).
- `cargo build --release` — exit 0.
- `cargo check -p luxurygram-shell` → exit 0 with the pinned 0.3.0 crates.
- `rust-anything app record rust --inputs … --no-traps -- …` — 7 cases recorded.
- `rust-anything app replay rust` — 7/7 (original).
- `rust-anything app replay rust --build "target/release/zxcvbn-translit.exe"`
  — 7/7 (Rust).
- The full-workspace `cargo test`/`cargo clippy` re-run after adding GPUI deps
  was interrupted once by the user, then completed successfully after the game:
  `cargo test --workspace` 7/7 and `cargo clippy --workspace --all-targets --
  -D warnings` exit 0.

## Known gaps

- **The GPUI-CE shell is compiled, not visually verified.** It builds with
  pinned 0.3.0 sources and its own examples were checked, but no window was
  launched and no UI parity with Qt exists yet.
- **rust-anything cannot parse C++/Qt.** Only black-box CLI recording and a
  future flat C ABI seam are usable. Everything ported so far uses the
  black-box path; UI equivalence needs separate visual tests.
- `rust/verification/zxcvbn-translit/reference/` holds a locally built C++
  reference binary and is gitignored; rebuild with `build-reference.ps1`.
- Only 7 hand-written cases; no fuzzing or real dictionary corpus yet.
- The M0 window has been compiled but not visually launched, so no screenshot
  or frame-time evidence exists yet.
- Design tokens are generated but not yet applied to any view; composite style
  values (`margins()`, `point()`, `icon()`, colors) are not extracted.
- The GPUI dependency tree adds 723 locked packages and a heavy first build;
  CI/fresh-machine cost has not been measured.

## Next checkpoint

1. Launch the M0 shell once and capture a screenshot/frame-time evidence;
   then move verified GPUI-CE API facts from the draft into `MAPPING.md`.
2. Apply design tokens to a first real view (not a placeholder) and extend the
   generator to composite style values.
3. Get explicit user confirmation on ADR-004 before adding any MTProto/crypto
   dependency; then start M1 with `grammers-*`.
4. Expand `FORK_DELTA.md` from a path inventory into a classified per-feature
   delta, with fork-only features given port priority.
5. Keep the commit+push checkpoint discipline after each verified step.

## Blocked

- ADR-004 awaits user confirmation before adding MTProto dependencies.
