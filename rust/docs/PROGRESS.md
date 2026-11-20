# Port progress

Branch: `luxurygram-rust` (created from `dev` at `779e13d4c5`).
Goal: `goal-ee1acc77-35bc-46e8-9b6a-72fc9c3045f5` (full Rust rewrite, 100 rounds).

## Milestones

| ID | Scope | Status |
| --- | --- | --- |
| M0 | Workspace, build/CI discipline, first behavior-proven port | **in progress** — first slice done |
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

## Checks actually run

- `cargo test --workspace` — 6 passed, 0 failed.
- `cargo clippy --workspace --all-targets -- -D warnings` — exit 0.
- `cargo build --release` — exit 0.
- `rust-anything app record rust --inputs … --no-traps -- …` — 7 cases recorded.
- `rust-anything app replay rust` — 7/7 (original).
- `rust-anything app replay rust --build "target/release/zxcvbn-translit.exe"`
  — 7/7 (Rust).

## Known gaps

- **No GPUI-CE window yet.** M0 currently proves logic, not UI. The API has
  only been researched from published docs, not from pinned local sources.
- **rust-anything cannot parse C++/Qt.** Only black-box CLI recording and a
  future flat C ABI seam are usable. Everything ported so far uses the
  black-box path; UI equivalence needs separate visual tests.
- `rust/verification/zxcvbn-translit/reference/` holds a locally built C++
  reference binary and is gitignored; rebuild with `build-reference.ps1`.
- Only 7 hand-written cases; no fuzzing or real dictionary corpus yet.

## Blocked

- Nothing currently. ADR-004 awaits user confirmation before adding MTProto
  dependencies.
