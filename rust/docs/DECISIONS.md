# Decisions (ADRs)

## ADR-001 — Work on a separate branch (2026-11-20)

**Decision:** all Rust port work happens on `luxurygram-rust`, created from
`dev` at `779e13d4c5`. Pre-existing untracked files stay untouched.
**Why:** user request; keeps the C++/Qt product branch reviewable.

## ADR-002 — rust-anything is a black-box oracle here (2026-11-20)

**Context:** rust-anything 2.1.0 supports unit ports for TS/JS, Python, C, Go,
Ruby, Java and Kotlin, and whole-app CLI/server record/replay in any language.
There is no C++/Qt parser, bridge, recorder or UI driver (`src/lang/mod.rs`
has no Cpp variant; STATUS.md lists no C++).
**Decision:** use `app record` / `app replay` with deterministic CLI seams;
use the C unit path only if a flat `extern "C"` facade is extracted later.
Never claim a recording proves Qt behavior.
**Evidence:** https://raw.githubusercontent.com/erkamyaman/rust-anything/main/README.md
and https://raw.githubusercontent.com/erkamyaman/rust-anything/main/docs/whole-apps.md

## ADR-003 — GPUI-CE for the UI, pinned and time-boxed (2026-11-20)

**Decision:** target `gpui-ce` 0.3.0 (crate imported as `gpui`) for the new UI;
pin an exact version/revision once sources are vendored. Treat it as a
time-boxed proof of concept until 1.0 or 2–3 stable releases.
**Why:** retained element tree, entity/notify state, bottom-anchored
virtualized chat list (`ListAlignment::Bottom`, `FollowMode::Tail`) fit a chat
client. Risks: pre-1.0 breaking changes, small maintainer team, text stack in
flux, MSRV 1.95 (local cargo is 1.99, so OK).
**Evidence:** https://crates.io/crates/gpui-ce ,
https://docs.rs/gpui-ce/0.3.0/gpui/_documentation/guides/lists_and_scrolling/index.html

## ADR-004 — MTProto via grammers, not from scratch (2026-11-20)

**Context:** user advice (Gemini 4 hydrogen) says use grammers, not handwritten
MTProto/crypto. Verified: `grammers-client` 0.12.0 published 2026-11-08,
285k total downloads, MIT OR Apache-2.0, Rust, edition 2024. The GitHub repo
`Lonami/grammers` is archived as of 2026-11-20, but crate metadata points to
active development at `https://codeberg.org/Lonami/grammers` — so "alive" is
true with a hosting caveat.
**Decision (proposed, needs user confirmation):** use `grammers-*` crates for
MTProto/TL/session; keep credentials in environment variables. Do not implement
crypto or the wire protocol ourselves. Re-evaluate if Codeberg activity stops.
**Evidence:** https://crates.io/api/v1/crates/grammers-client ,
https://api.github.com/repos/Lonami/grammers

## ADR-005 — GPL-3.0, preserve attribution (2026-11-20)

Root `LICENSE` is GPL-3.0; the port is a derivative work. Ported files keep
the Telegram Desktop header and link to `LEGAL`. Workspace manifests declare
`GPL-3.0-only`.

## ADR-006 — First port: zxcvbn_translit (2026-11-20)

A standalone C++ CLI in the repo (`Telegram/SourceFiles/_other/zxcvbn_translit.cpp`
plus `ui/passcode_strength_translit.cpp`) is pure logic, deterministic and
already has a file-based CLI. It gives a real, checkable black-box oracle
without Qt. Proven 7/7 against the original binary.

## ADR-007 — Mandatory intermediate commits and pushes (2026-11-20)

User request. Added as rule 7 of the root `AGENTS.md`. Commit only verified,
task-owned changes; push the task branch at each checkpoint. No release, tag
or PR without a separate explicit request.
