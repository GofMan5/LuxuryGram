# C++/Qt → Rust / GPUI-CE 0.3.0 Mapping

**Status:** Verified against pinned Cargo registry sources (`gpui-ce = 0.3.0`, `gpui_ce_platform = 0.3.0`) and the workspace `design-tokens` crate.  
**Scope note:** Every Rust / GPUI-CE symbol and signature below was verified directly in the local sources cited (`file:line`). Items not present in these crates (or not yet implemented in this repo) are explicitly marked as **Gap / Unknown**.

## Pinned Crates & Entry Points

- `apps/luxurygram-shell/Cargo.toml:9-10` aliases `gpui = { package = "gpui-ce", version = "=0.3.0" }` and `gpui_platform = { package = "gpui_ce_platform", version = "=0.3.0" }`.
- `gpui_ce_platform-0.3.0` uses `src/gpui_platform.rs` as its crate root (`Cargo.toml:59-62`; there is no `src/lib.rs`), exporting:
  - `application() -> gpui::Application` (`src/gpui_platform.rs:64`)
  - `headless() -> gpui::Application` (`src/gpui_platform.rs:85`)
  - `background_executor() -> gpui::BackgroundExecutor` (`src/gpui_platform.rs:46`)
  - `linux(allowed_modes: gpui::WindowingModes) -> gpui::Application` (`src/gpui_platform.rs:95`, gated to `linux`/`freebsd`)
- `gpui-ce-0.3.0/src/prelude.rs:5-9` re-exports: `AppContext as _`, `BorrowAppContext`, `Context`, `Element`, `InteractiveElement`, `IntoElement`, `ParentElement`, `Refineable`, `Render`, `RenderOnce`, `StatefulInteractiveElement`, `Styled`, `StyledImage`, `TaskExt as _`, `VisualContext`, and `util::FluentBuilder`.

---

## Summary Matrix

| # | C++/Qt Concept | Verified GPUI-CE 0.3.0 / Workspace Counterpart | Status / Key Gap |
| --- | --- | --- | --- |
| 1 | `rpl::producer` / `rpl::variable` | `Entity<T>` + `cx.notify()`, `cx.observe()` / `observe_self()`, `Subscription` | **Verified**; no built-in reactive stream combinator library in `gpui-ce` (`rpl` pipelines/map/combine must be modeled explicitly or via async channels/tasks). |
| 2 | Qt signals/slots & `QAction` | `EventEmitter<E>` + `cx.emit(event)` + `cx.subscribe()`; `actions!` / `Action` | **Verified**; event delivery is queued until after the current update finishes, not synchronous direct-connection dispatch. |
| 3 | `RpWidget` hierarchy & layout | `Render` (stateful view) / `RenderOnce` + `#[derive(IntoElement)]` (stateless), `div()`, `ParentElement`, `Styled` | **Verified**; flexbox/taffy layout via `Style`/`StyleRefinement` instead of Qt `resizeEvent`/manual geometry. |
| 4 | Chat message list (history/feed) | `list()` + `ListState` (`ListAlignment::Bottom`, `FollowMode::Tail`, `ListSizingBehavior`) + `uniform_list()` | **Verified**; `list()` handles variable-height chat rows and tail-following; `uniform_list()` requires uniform row height. |
| 5 | `anim::value` / frame animations | `Animation` + `AnimationExt` (`with_animation`, `with_animations`, `with_spring`), `Animated<T, Time>`, `Transition<T>` / `use_keyed_transition` | **Verified**; `App::reduce_motion` is not read from the OS automatically (`src/elements/animation.rs:107-108`). |
| 6 | `crl::on_main` / `crl::async` | `cx.spawn` / `App::spawn` / `Window::spawn` (foreground) and `cx.background_spawn` / `BackgroundExecutor::spawn` + `Task` | **Verified**; dropping `Task<R>` cancels unless `.detach()` or `TaskExt::detach_and_log_err()` is called. |
| 7 | Rich text / shaping / bidi | `Text`, `StyledText` (`with_highlights`, `with_default_highlights`, `with_runs`, `with_font_family_overrides`), `TextRun`, `Font`, `HighlightStyle`, `TextSystem`, Parley (`ParleyTextSystem`) | **Verified**; Telegram `TextWithEntities` parsing/click-entity hit-testing must be bridged on top of `StyledText::layout()` and byte-range runs. |
| 8 | `.style` metrics | `design-tokens` crate (`rust/crates/design-tokens/src/lib.rs`) | **Partial**; 51 scalar `f32` tokens generated; composite `.style` types (`margins`, `point`, `icon`, colors) are **Gap**. |
| 9 | Custom painting / frame timing / bench | `canvas()` / `Canvas<T>`, `Element` (`request_layout`/`prepaint`/`paint`), `window.on_next_frame()`, `window.request_animation_frame()`, `bench-support` (`BenchAppContext`, `BenchReport`, `bench_metrics`) | **Verified**; frame timing & alloc/perf counters require `--features bench-support`. |
| — | Localization `tr::` | No localization crate or `tr::` generator in `rust/` yet | **Gap / Unmapped**; target format and runtime locale-switch mechanism remain undecided. |

---

## Domain Details (Verified)

### 1. Reactive State (`rpl::producer` / `rpl::variable`) → `Entity<T>` & Observers

- **Owned reactive model (`Entity<T>` + `Context<T>`):**
  - `AppContext::new` creates an entity with `&mut Context<T>` (`gpui-ce-0.3.0/src/gpui.rs:268`; implemented on `Context` at `src/app/context.rs:1171`).
  - Mutate via `Entity::update` (`src/app/entity_map.rs:623`), `Entity::update_in` (`src/app/entity_map.rs:666`), `Entity::as_mut` (notifies on guard drop, `src/app/entity_map.rs:637`), or `Entity::write` (replaces value and notifies, `src/app/entity_map.rs:654`).
  - Trigger redraws/observers with `cx.notify()` (`Context::notify`, `src/app/context.rs:408`).
- **Observing changes:**
  - `Context::observe(&mut self, entity: &Entity<W>, on_notify)` (`src/app/context.rs:147`) and `Context::observe_self` (`src/app/context.rs:166`) run after the update that called `notify` finishes.
  - Window-level observers: `Context::observe_window_appearance` (`src/app/context.rs:756`) and `Context::observe_window_display` (`src/app/context.rs:776`).
  - Teardown hook: `Context::on_release` (`src/app/context.rs:271`) runs when the last strong `Entity<T>` handle is dropped.
- **Subscription lifetime (`rpl::lifetime` equivalent):**
  - `Subscription` (`src/subscription.rs:156`) is `#[must_use]` and cancels on `Drop` (`src/subscription.rs:216-221`).
  - Store `Subscription` fields on the owning entity to tie callback lifetime to the view/entity, call `Subscription::join` (`src/subscription.rs:200`) to combine handles, or call `Subscription::detach` (`src/subscription.rs:192`) to leave it registered until entity release.
  - Internal subscriber storage uses `SubscriberSet<EmitterKey, Callback>` (`src/subscription.rs:9`).
- **Gap / Difference vs. `rpl`:** `gpui-ce` provides state invalidation (`notify`/`observe`) and discrete typed events (`emit`/`subscribe`), not pull/push stream combinators (`rpl::map`, `rpl::combine`, `rpl::distinct_until_changed`). Multi-value streams require an async channel + foreground `cx.spawn` task or explicit state fields on an `Entity<T>`.

### 2. Qt Signals/Slots & `QAction` → `EventEmitter<E>`, `subscribe`, & `actions!`

- **Typed entity events (signals/slots):**
  - Declare `impl EventEmitter<MyEvent> for MyEntity {}` (`EventEmitter<E: Any>: 'static` at `gpui-ce-0.3.0/src/gpui.rs:516`).
  - Emit with `cx.emit(event)` (`Context::emit`, `src/app/context.rs:1160`).
  - Subscribe with `cx.subscribe(&target_entity, |this, emitter, evt, cx| ...)` (`Context::subscribe`, `src/app/context.rs:227`) or `cx.subscribe_self` (`src/app/context.rs:248`).
  - Delivery order: events are delivered **after** the current `update` finishes, in emission order (`src/app/context.rs:1118-1120`).
- **UI input callbacks & command actions:**
  - `Context::listener(|this, event: &ClickEvent, window, cx| ...)` (`src/app/context.rs:513`) holds a weak handle to `this` so UI closures do not create reference cycles; `Context::processor` (`src/app/context.rs:530`) holds a strong handle when a return value is required.
  - `ClickEvent` (`src/interactive.rs:363`) covers `Mouse(MouseClickEvent)`, `Keyboard(KeyboardClickEvent)`, and reserved `Touch(TouchClickEvent)`.
  - Unit command actions: `actions!(namespace, [ActionA, ActionB])` (`src/action.rs:34`) or `#[derive(Action)]` for actions carrying data (`Action` trait at `src/action.rs:52`).

### 3. `RpWidget` Hierarchy → `Render`, `RenderOnce`, `IntoElement`, `div()`, `ParentElement`, `Styled`

- **Stateful views (`RpWidget` with internal state/entity):**
  - Implement `Render` (`gpui-ce-0.3.0/src/element.rs:391`), returning `impl IntoElement` from `fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement`.
  - Do not mutate state inside `render`, as GPUI may call or skip it arbitrarily (`src/element.rs:395-396`).
- **Stateless/reusable components:**
  - Implement `RenderOnce` (`src/element.rs:463`, `fn render(self, window: &mut Window, cx: &mut App) -> impl IntoElement`) and derive `IntoElement` (`src/element.rs:321`). Pass callbacks rather than calling `cx.notify()` (`src/element.rs:411-413`).
  - Use `IntoElement::into_any_element` (`src/element.rs:337`) when branches return different concrete element types.
- **Container & styling primitives:**
  - `div() -> Div` (`src/elements/div.rs:2598`, `Div` at `2613`) is the flexbox container; attaching `.id(...)` upgrades it to `Stateful<Div>` (`src/elements/div.rs:2611-2612`).
  - `ParentElement` (`src/element.rs:506`) provides `.child(...)` and `.children(...)`.
  - `Styled` (`src/styled.rs:81`) provides builder methods writing into `&mut StyleRefinement` and `.refine_style(&StyleRefinement)`.
- **Low-level custom widget:**
  - Implement `Element` (`src/element.rs:215, 229, 245`) with the three explicit phases: `request_layout` → `prepaint` (insert hitboxes) → `paint` (issue draw primitives and frame-scoped listeners).

### 4. Chat Message List (`HistoryWidget` / `ListWidget`) → `list()`, `ListState`, & `uniform_list()`

- **Variable-height chat feed (`list` + `ListState`):**
  - Constructor: `list(state: ListState, render_item: impl FnMut(usize, &mut Window, &mut App) -> AnyElement + 'static) -> List` (`gpui-ce-0.3.0/src/elements/list.rs:75`).
  - `List::with_sizing_behavior(ListSizingBehavior)` (`src/elements/list.rs:100`), where `ListSizingBehavior` (`src/elements/list.rs:262`) is `Infer` or `Auto` (default).
  - `ListState` (`src/elements/list.rs:112`, interior-mutable `Rc<RefCell<StateInner>>`):
    - `ListState::new(item_count, alignment, overdraw)` (`src/elements/list.rs:405`), with `ListAlignment::Top` or `ListAlignment::Bottom` (`src/elements/list.rs:234`; `Bottom` anchors chat logs at the last row).
    - Tail following: `state.set_follow_mode(FollowMode::Tail)` (`src/elements/list.rs:865`, `FollowMode::{Normal, Tail}` at `183`) keeps the end in view as rows append and automatically pauses/resumes based on user scroll; `state.scroll_to_end()` (`src/elements/list.rs:838`) anchors to the end even while the last row grows.
    - Incremental updates: `state.splice(old_range, count)` (`src/elements/list.rs:653`) and `state.splice_focusable(...)` (`src/elements/list.rs:662`); `state.reset(element_count)` (`src/elements/list.rs:471`) replaces all rows and resets scroll.
    - Height invalidation: `state.remeasure()` (`src/elements/list.rs:517`, proportional scroll anchor) and `state.remeasure_items(range)` (`src/elements/list.rs:528`, absolute scroll anchor).
    - Height estimation / measurement: `state.with_uniform_item_height(height)` (`src/elements/list.rs:450`) or `state.measure_all()` (`src/elements/list.rs:432`, sets `ListMeasuringBehavior::Measure`, default is `Visible` at `274`).
    - Scroll callbacks & position: `state.set_scroll_handler(...)` (`src/elements/list.rs:745`, receives `ListScrollEvent` at `245`) and `state.logical_scroll_top()` (`src/elements/list.rs:757`).
- **Fixed-height lists (`uniform_list`):**
  - `uniform_list(id, item_count, f: Fn(Range<usize>, &mut Window, &mut App) -> Vec<R>) -> UniformList` (`src/elements/uniform_list.rs:72`).
  - Uses `UniformListScrollHandle` (`src/elements/uniform_list.rs:137`) wrapping `UniformListScrollState` (`174`, includes `y_flipped: bool` and `deferred_scroll_to_item`) and `ScrollStrategy::{Top, Center, Bottom, Nearest}` (`src/elements/uniform_list.rs:144`).
  - Horizontal sizing uses `ListHorizontalSizingBehavior::{FitList, Unconstrained}` (`src/elements/list.rs:296`).

### 5. `anim::value` & Animations → `AnimationExt`, `Animated<T, Time>`, & `Transition<T>`

- **Duration & animation configuration:**
  - `secs(seconds: u64)` (`gpui-ce-0.3.0/src/motion.rs:24`) and `millis(milliseconds: u64)` (`src/motion.rs:42`).
  - `Animation::new(motion)` (`src/elements/animation.rs:38, 53`) with `.repeat()` (`65`), `.repeat_synced()` (`74`, shared `App` phase), `.with_easing(...)` (`85`), and `.with_max_fps(f32)` (`94`).
- **Declarative element animations (`AnimationExt`):**
  - Implemented for all `IntoElement` (`src/elements/animation.rs:109`):
    - `.with_animation(id, animation, |el, delta| ...)` -> `AnimationElement<Self>` (`src/elements/animation.rs:152, 284`).
    - `.with_animations(id, animations, |el, step, delta| ...)` (`src/elements/animation.rs:222`).
    - `.with_spring(id, spring_animation, |el, val| ...)` -> `SpringAnimationElement<Self>` (`src/elements/animation.rs:247, 294`).
  - Reduce-motion caveat: `AnimationExt` honors `App::reduce_motion`, **but GPUI does not read the OS reduce-motion setting**; the application must call `App::set_reduce_motion` itself (`src/elements/animation.rs:105-108`).
- **Imperative value animations (`anim::value` direct equivalents):**
  - `Animated<T, Time = Instant>` (`src/animated.rs:45`, `new` at `61`, `sample(now) -> AnimatedSample<T>` at `183`, `AnimatedSample<T>` at `7`) interpolates toward a logical target and smoothly retargets mid-run from the currently presented value (`src/animated.rs:21-23`).
  - `Transition<T>` (`src/transition.rs:99`, `new` at `117`, `with_easing` at `130`, `continuous` at `140`) wraps `Entity<TransitionState<T>>` with per-render caching.
  - Inside `render`, create persistent transitions with `window.use_transition(cx, motion, init)` (`src/window.rs:5722`) or `window.use_keyed_transition(key, cx, motion, init)` (`src/window.rs:5744`).

### 6. `crl::on_main` / `crl::async` → Foreground & Background Executors + `Task`

- **Main-thread futures (`crl::on_main`):**
  - From an entity context: `cx.spawn(async move |weak_this, cx: &mut AsyncApp| ...)` (`Context::spawn`, `gpui-ce-0.3.0/src/app/context.rs:461`; weak-entity error handling documented at `414-416`).
  - From `App`: `App::spawn(async move |cx: &mut AsyncApp| ...)` (`src/app.rs:2871`); end-of-effect-cycle synchronous deferral via `App::defer` (`src/app.rs:2916`) or `Context::defer_in` (`src/app/context.rs:586`).
  - From `Window`: `window.spawn(cx, async move |cx: &mut AsyncWindowContext| ...)` (`src/window.rs:3556`).
  - Low-level executor: `ForegroundExecutor::spawn` (`src/executor.rs:39, 550`).
- **Worker-thread futures (`crl::async`):**
  - `cx.background_spawn(future)` (`AppContext::background_spawn`, `src/gpui.rs:392`; `Context` impl at `src/app/context.rs:1258`) or `BackgroundExecutor::spawn` (`src/executor.rs:25, 194`) via `App::background_executor()` (`src/app.rs:424, 2769`) or `AsyncApp::background_executor()` (`src/app/async_context.rs:233`).
  - Futures must be `Send + 'static` and have no `App` access; `.await` the returned `Task<R>` from a foreground future to update entities (`src/executor.rs:19-21, 181-182`).
  - For work needing a 2 MiB default std stack instead of shared worker stacks, use `BackgroundExecutor::spawn_dedicated` (`src/executor.rs:160`).
- **Cancellation (`Task<R>` & `TaskExt`):**
  - `Task` is re-exported from `scheduler::Task` (`src/executor.rs:14`). **Dropping a `Task` cancels the future** (`src/app/context.rs:418-420`, `src/executor.rs:182`).
  - Store `Task` on the owning entity to auto-cancel on replacement/drop, call `.detach()`, or use `TaskExt::detach_and_log_err(cx)` / `detach_and_log_err_with_backtrace(cx)` (`src/executor.rs:94-102`).

### 7. Rich Text, Shaping & Bidi (`TextWithEntities`) → `StyledText`, `TextRun`, & Parley

- **Plain vs. rich text elements:**
  - `Text` (`gpui-ce-0.3.0/src/elements/text.rs:61`, `Text::new` at `72`, `Text::new_inaccessible` at `83`) for plain accessible/decorative text.
  - `StyledText::new(text)` (`src/elements/text.rs:414, 427`) for multi-run styled text:
    - `.with_highlights(iter)` (`src/elements/text.rs:496`) overlays sorted, non-overlapping UTF-8 byte ranges with `HighlightStyle` (`src/style.rs:857`) onto inherited `TextStyle`.
    - `.with_default_highlights(&text_style, iter)` (`src/elements/text.rs:459`) computes runs immediately from an explicit `TextStyle`.
    - `.with_font_family_overrides(iter)` (`src/elements/text.rs:552`) overrides font families by byte range.
    - `.with_runs(Vec<TextRun>)` (`src/elements/text.rs:613`) supplies explicit `TextRun` values (`src/text_system/text_run.rs:37`) whose `len` values must sum exactly to `text.len()` on UTF-8 char boundaries.
- **Fonts, shaping, & bidi:**
  - `Font` (`src/text_system/font.rs:39`) specifies `family` (default `.SystemUIFont`), `features`, `fallbacks`, `weight`, `width`, and `style`.
  - `TextSystem` (`src/text_system/text_system.rs:19`) provides `add_fonts` (`81`), `all_font_names` (`43`), `bounding_box` (`157`), `typographic_bounds` (`167`), and `advance` (`193`).
  - `TextLayoutOptions` (`src/text_system/text_layout.rs:28`) exposes `wrap_width`, `line_clamp`, `alignment_width`, `text_align`, `direction: ParagraphDirection`, and `unicode_bidi: UnicodeBidi`.
  - Parley integration: `ParleyTextSystem` in `gpui_ce_parley` provides real cross-platform font shaping (`src/app/headless_app_context.rs:34`); `gpui_ce_platform-0.3.0/tests/parley_render.rs:15-55` exercises mixed-direction Arabic/Hebrew/Latin bidi and emoji fallback (`Noto Color Emoji`).
- **Gap vs. Telegram `TextWithEntities`:** `StyledText` handles visual styling, wrapping, truncation (`StyledText::apply_truncation`, `src/elements/text.rs:990`), and layout-coordinate hit-testing via `StyledText::layout()` (`src/elements/text.rs:442`), but Telegram entity semantics (links, mentions, spoilers, custom emoji, copyable code blocks) and click/hover dispatch on entity spans must be built in application code.

### 8. `.style` Metrics → `design-tokens` Crate

- `rust/crates/design-tokens/src/lib.rs:1-117` (generated by `rust/tools/generate-tokens.ps1:1-73`) extracts **51** `pub const ...: f32` scalar tokens (`px` and `unitless`) from:
  - `Telegram/SourceFiles/luxury/ui/luxury_styles.style`
  - `Telegram/SourceFiles/luxury/ui/luxury_icons.style`
  - `Telegram/SourceFiles/luxury/ui/settings/luxury_settings.style`
- **Explicit gap (`rust/crates/design-tokens/src/lib.rs:3`):** Composite `.style` values (`margins`, `point`, `icon`, colors, and upstream non-fork `.style` files) are **not covered yet**.

### 9. Custom Painting, Frame Timing, & Benchmarking Hooks

- **Custom painting:**
  - `canvas(prepaint, paint) -> Canvas<T>` (`gpui-ce-0.3.0/src/elements/canvas.rs:62, 78`): runs `prepaint` once bounds are resolved and `paint` with those bounds + prepaint state; size it with `Styled` methods (e.g. `.size_full()`), or implement `Element` directly when cross-frame state is needed (`src/elements/canvas.rs:16-17, 76-77`).
  - `Window::paint_layer(bounds, |window| ...)` (`src/window.rs:5818`) groups clipped draw primitives into a renderer layer.
- **Frame timing:**
  - `window.on_next_frame(callback)` (`src/window.rs:3505`) schedules a one-shot callback on the next frame and wakes the platform frame source.
  - `window.request_animation_frame()` (`src/window.rs:3529`) marks the current rendering/painting view dirty for the next draw and notifies it via `on_next_frame` (panics if called outside a view render/paint).
  - `cx.on_next_frame(window, |this, window, cx| ...)` (`src/app/context.rs:570`) wraps `window.on_next_frame` for an entity.
- **Benchmarking hooks (`bench-support`):**
  - Feature flags: `gpui-ce-0.3.0/Cargo.toml:54` (`bench-support = ["profiler", "dep:criterion", "dep:libc", "dep:perf-event"]`) and `gpui_ce_platform-0.3.0/Cargo.toml:28-33`.
  - Context & reporting (`gpui-ce-0.3.0/src/app/bench_context.rs`): `bench_platform` (`47`), `ForegroundWorkSummary` (`78`), `BenchReport` (`107`, `with_fps` at `125`), and `BenchAppContext` (`509`, used by `#[gpui::bench]`).
  - Metrics (`gpui-ce-0.3.0/src/bench_metrics.rs:4-11`, gated behind `#[cfg(feature = "bench-support")]` in `src/gpui.rs:28-29`): `BenchMeasurement`, `MetricReport`, `HardwareCounter`, `ResourceCounter`, `CountingAllocator`, and `AllocationCounter`.
  - Reference platform benches in `gpui_ce_platform-0.3.0/Cargo.toml:69-79`: `benches/inspector_render.rs` and `benches/randomized_element_tree.rs`. (`gpui-ce-0.3.0` itself ships no `examples/` or `benches/`.)
