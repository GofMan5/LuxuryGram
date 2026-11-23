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

## ADR-008 — Pin GPUI-CE 0.3.0 exactly (2026-11-20)

The published 0.3.0 "Start a project" guide still shows `gpui-ce 0.2` and
`gpui_ce_platform 0.1`. crates.io metadata shows the actual current pair is
`0.3.0`/`0.3.0`, both published 2026-10-20, Apache-2.0, edition 2024.
**Decision:** use exact `=0.3.0` pins for both packages and verify every API
against the downloaded crate's `examples/` and `src/` before use. `cargo check
-p luxurygram-shell` passed with these pins; the guide's stale versions were
not used.

## ADR-009 — Generate fork design tokens from `.style` (2026-11-20)

**Decision:** extract only top-level scalar declarations from the three
fork-specific style files (`luxury_styles.style`, `luxury_icons.style`,
`luxury_settings.style`) into a `design-tokens` crate with a checked-in
generator (`rust/tools/generate-tokens.ps1`). Every generated constant keeps
its source file and line in a doc comment. Composite values stay a documented
gap; no raw pixel constants are added to Rust views instead.

## ADR-010 — Hybrid bottleneck optimization replaces the full rewrite (2026-11-20)

**User decision:** stop treating "rewrite all 2.9M lines" as the objective.
Replace only the bottlenecks, keep proven native libraries where there is no
speed to gain.

| Area | Decision | Rationale |
| --- | --- | --- |
| UI / compositing | Rust + GPUI-CE instead of Qt/QPainter | GPU rendering of bubbles, blur, glyphs and texture atlases; target smooth 4K/120Hz scrolling |
| MTProto / TL | Rust with `grammers-*` + tokio | async deserialization of large update batches off the main thread |
| Message/chat storage | LSM (Fjall or RocksDB) + Zstd | compact sequential writes, no VACUUM-style blocking, 3–5x less disk for repetitive text |
| Full-text search | Tantivy | indexed search instead of linear scans; morphology-aware |
| Media cache | content-addressed store by SHA-256, WebP/AVIF previews | deduplicate reposts, keep large blobs out of the LSM cache |
| Media decoding / calls | keep FFmpeg, WebRTC, dav1d, rlottie as C/C++ behind FFI | already SIMD-optimized; rewriting gives no benefit |

**Consequences:** the M0 GPUI shell and generated design tokens remain valid
(first UI slice). The old M1–M7 monolithic milestones are replaced by measured
bottleneck slices. Every slice needs a baseline measurement and a focused
before/after check; no component is migrated just for the sake of "all Rust".
rust-anything stays useful only for CLI/C-ABI oracles, not for Qt proof.

## ADR-011 — fast-anything for measured function-level ports (2026-11-23)

**User request:** use `EL4CTEO/fast-anything` on LuxuryGram and rewrite what is
slow in Rust. Verified facts in `docs/fast-anything-trial.md`.

**Decision:** use fast-anything's scan → port → verify → enable → bench loop for
hot functions that can be compiled into a standalone C/C++ command, but enable a
port **only when `fast-anything bench` beats the original end to end**. The tool's
C bridge serializes arguments and results through JSON; per-call and per-byte
cost is high. A correct but bridge-bound port must be disabled (as happened with
`TransformDictionary`: 1.2× faster per call, 0.8× slower whole app).

**Consequences:** a small behavior-preserving refactoring of
`zxcvbn_translit.cpp` is kept (two pure functions, byte-identical output); the
port crates and recordings live in gitignored `.fast/`. Candidate selection must
favor compute-dense work (small inputs, heavy CPU), not streaming transforms.
This does not replace ADR-010's slice plan; it is the tool for proving individual
function ports inside those slices. ADR-004 still needs user confirmation before
any MTProto/network dependency is added.

## ADR-012 — binary C ABI for verified ports (2026-11-23)

**Context:** ADR-011's verified Rust port lost end to end only because
fast-anything's generated C bridge JSON-serializes the whole argument and
result. The function itself was correct (786/786 calls matched).

**Decision:** keep fast-anything for profiling, recording and verification, but
ship verified ports through a minimal binary C ABI (raw byte pointers and
lengths, one crossing per batch) with an allocation-free Rust implementation.
For `zxcvbn_translit` this yields 1.78× on an English dictionary and 2.52× on a
Cyrillic one in-process, with byte-identical output. The call site is opt-in via
`LUXURY_ZXCVBN_RUST` / `LUXURY_ZXCVBN_RUST_DLL` and falls back to the original
C++ when disabled or unavailable; no build system or runtime product behaviour
changes in this checkpoint.

**Consequences:** fast-anything remains the proof tool; its generated bridge is
not the shipping transport for this repo. Future ports should measure both the
tool's own `bench` and an in-process binary-ABI benchmark before being enabled.
Build-system integration (shipping the cdylib with the CLI) is a separate step.
