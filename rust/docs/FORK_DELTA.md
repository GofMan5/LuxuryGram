# Fork delta — preliminary inventory

**Method and honesty note.** The fork's merge base with `upstream/dev` is
`3a2a9a34e833d3243a13787215b8a7cdcf5dc3c3`; `git diff --shortstat <base> HEAD`
reports 1178 files changed, 518659 insertions, 34339 deletions. That diff is
too large to classify in one pass, so this is an **inventory of the fork-owned
areas**, not a complete delta. It must be expanded before any claim that fork
behavior has been fully ported. The upstream checkout is not a behavioral
reference: fork-specific behavior is defined by this repository's code and
history (per the user's instruction and the root `AGENTS.md`).

## Fork-owned areas found

- `Telegram/SourceFiles/luxury/` — 220 tracked files. Subareas:
  - `data/` — fork database/messages storage and entities.
  - `features/` — filters, forward, message shot, streamer mode, translator,
    watch/presence history.
  - `libs/` — vendored JSON helpers and QuickJS/SQLite libraries under
    `libs/quickjs/`, `libs/sqlite/`.
  - `plugins/` — plugin system integration.
  - `ui/` — fork-specific surfaces.
  - `utils/` — `luxury_mapper`, `telegram_helpers`, Windows helpers.
- Recent fork commits touching those areas (from `git log`):
  - `779e13d4c5` Fix Chat Analysis attribution, advice and scoped export.
  - `e938cf6efa` feat(plugins): improve chat analysis and draft refinement.
  - `d45624e596` fix(plugins): close downloaded files before install swap.
  - `4e00216cce` fix(player): save and open the full-size album cover.
  - `61969fc4dc` Merge net/wstransport: WebSocket MTProto transport.
  - `4964248fba` Settings: WebSocket transport mode, status line and log.
- `Telegram/SourceFiles/_other/` and `ui/` contain shared tools used by the
  fork; `zxcvbn_translit.cpp` and `passcode_strength_translit.cpp` were the
  first port because they are self-contained and have a CLI oracle.

## Priority for the port

1. Fork-only behavior in `luxury/` (highest): chat analysis, filters, forward,
   translator, plugin management, WebSocket transport, custom themes/tokens.
2. Shared logic with deterministic seams (e.g. build tools, parsers) — good
   candidates for rust-anything black-box proofs.
3. Upstream-compatible protocol, storage and UI, which should be carried over
   through reviewed crates rather than rewritten from memory.

## Not yet done

- Classify every changed file as fork-owned / upstream sync / generated.
- Locate the exact AyuGram base commit used by this fork.
- Record custom `.style` tokens and custom themes in `MAPPING.md`.
