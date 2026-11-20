# C++/Qt → Rust mapping (research draft)

**Status:** draft. No `gpui-ce` source is vendored locally yet, so every API
name below comes from published docs research and is **unverified**. Before
using any of it in code, check the pinned sources/examples and update this
file; if it cannot be verified, say so rather than guessing.

| C++/Qt concept | Candidate Rust/GPUI-CE counterpart | Verification needed |
| --- | --- | --- |
| `rpl::producer` / `variable` | `Entity<T>` + `cx.notify()`; subscriptions via `EventEmitter` | confirm entity/context lifetimes and how a stream of values is represented |
| Qt signals/slots | typed events through `EventEmitter` / `cx.emit` / subscribe | confirm trait imports in 0.3.0 (`prelude::*`) |
| `RpWidget` hierarchy | `Render` views returning element trees from `div()` | confirm `RenderOnce`, `IntoElement`, styling API |
| Chat message list | `list()` + `ListState`, `ListAlignment::Bottom`, `FollowMode::Tail` | docs.rs guide exists; verify exact constructor in pinned version |
| `anim::value` / animations | element transitions + `cx.notify()` redraw; custom `Element` for hard cases | confirm animation API and frame timing |
| `crl::on_main` / `crl::async` | `cx.spawn` (main thread) / `background_spawn` + `Task` | confirm cancellation semantics |
| Text entities / rich formatting | `StyledText` / `TextRun`, Parley shaping | confirm 0.3.0 text API; 0.4-dev breaks it |
| `.style` metrics | design tokens in a Rust module generated from `.style` files | decide generator; no raw pixel constants in views |
| Localization `tr::` | generated phrase tables + runtime locale switch | decide format; keep phrase keys stable |

Sources used for the draft:
- https://docs.rs/gpui-ce/0.3.0/gpui/ (API docs)
- https://docs.rs/gpui-ce/0.3.0/gpui/_documentation/guides/lists_and_scrolling/index.html
- https://gpui-ce.github.io/ (project site)

Do not add API calls to code from this table alone.
