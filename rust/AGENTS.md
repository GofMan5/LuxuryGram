# Rust port — workspace rules

The root `AGENTS.md` is canonical for the whole repository. This file adds rules
that apply only to work under `rust/`.

1. Read the whole C++ module (and its callers) before writing Rust. Port
   **behavior**, not class structure; write idiomatic Rust.
2. Never write `gpui-ce` API from memory. Check the pinned sources/examples
   under `vendor/` or the exact version's docs; if an API cannot be verified,
   say so and leave it as a documented gap.
3. `cargo check` after every edit. Before every commit: `cargo fmt --all`,
   `cargo clippy --workspace --all-targets -- -D warnings`, `cargo test --workspace`.
   Never commit a red tree.
4. No silent stubs. Every `todo!()`, deliberate simplification or unported edge
   case goes into `docs/PROGRESS.md` → **Known gaps** with the C++ location.
5. Do not write MTProto or crypto from scratch. Use a reviewed crate (current
   candidate: `grammers-*`, see `docs/DECISIONS.md` ADR-004). An ADR plus the
   user's confirmation is required before adding a new network/crypto dependency.
6. Avoid `unsafe`. Never panic on data that came from the network or disk.
7. One module per session; finish and verify it before claiming the next one.
8. After three identical failures, stop, record the condition in
   `docs/PROGRESS.md` → **Blocked** and ask the user.
9. Commit and push task-owned work at each verified checkpoint (mandatory).
   Stage only paths owned by the task; never stage the untracked
   `ayugram_issue_ports_regressions.py` or other pre-existing work.
10. End every progress answer with: what was done, the exact commands actually
    run and their results, what is next, and any blocking questions. Never say
    "done" without observed command output.

License: LuxuryGram is GPL-3.0 (see the root `LICENSE`). Keep the original
Telegram Desktop headers and attribution in ported files. API credentials come
from environment variables, never from committed source.
