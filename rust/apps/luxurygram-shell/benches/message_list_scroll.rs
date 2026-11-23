//! H1 baseline benchmark for virtualized chat message list scrolling in GPUI-CE 0.3.0.
//!
//! Uses `#[gpui::bench]` + `BenchAppContext::bench_renderer` against `gpui::list()`
//! (`ListAlignment::Bottom`, `ListState::scroll_by`) with synthetic message bubbles
//! containing sender metadata, variable-length body text (`StyledText`), and timestamps.
//!
//! Note on Windows (`gpui-ce = 0.3.0`, `gpui_ce_platform/src/gpui_platform.rs:239-242`):
//! `current_headless_renderer()` returns `Ok(None)` on Windows, so `bench_renderer`
//! measures layout, text shaping (`bench_text_system()`), prepaint, and scene building,
//! while GPU atlas upload / present is excluded until a windowed or headless GPU harness
//! is wired.

use std::{cell::Cell, fmt, rc::Rc, time::Duration};

use gpui::{
    AnyElement, BenchAppContext, Context, HighlightStyle, IntoElement, ListAlignment, ListState,
    ParentElement as _, Render, SharedString, Styled as _, StyledText, Window, colors::Colors, div,
    list, prelude::FluentBuilder as _, px, rgb_to_hsla,
};
use luxurygram_shell::feed::MessageFeed as ProductionFeed;

#[derive(Clone)]
struct SyntheticMessage {
    sender: SharedString,
    body: SharedString,
    highlight_len: usize,
    timestamp: SharedString,
    outgoing: bool,
}

#[derive(Clone, Copy)]
struct ScrollBenchCase {
    message_count: usize,
    scroll_step_px: f32,
}

impl fmt::Display for ScrollBenchCase {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}-msgs-step-{}px",
            self.message_count, self.scroll_step_px as u32
        )
    }
}

struct MessageFeed {
    messages: Rc<Vec<SyntheticMessage>>,
    list_state: ListState,
    rendered_items: Rc<Cell<usize>>,
    rendered_frames: Rc<Cell<usize>>,
    scroll_step_px: f32,
    scroll_direction: f32,
    scrolled_steps: usize,
}

impl MessageFeed {
    fn new(message_count: usize, scroll_step_px: f32) -> Self {
        let messages = Rc::new(build_synthetic_messages(message_count));
        let list_state = ListState::new(message_count, ListAlignment::Bottom, px(240.0));
        Self {
            messages,
            list_state,
            rendered_items: Rc::new(Cell::new(0)),
            rendered_frames: Rc::new(Cell::new(0)),
            scroll_step_px,
            scroll_direction: -1.0,
            scrolled_steps: 0,
        }
    }

    fn step_scroll(&mut self) {
        // Ping-pong through the history in deterministic steps so repeated
        // Criterion iterations keep visiting uncached/newly-exposed rows rather
        // than clamping at item 0.
        if self.scrolled_steps >= 200 {
            self.scroll_direction = -self.scroll_direction;
            self.scrolled_steps = 0;
        }
        self.list_state
            .scroll_by(px(self.scroll_step_px * self.scroll_direction));
        self.scrolled_steps += 1;
    }
}

impl Render for MessageFeed {
    fn render(&mut self, window: &mut Window, _cx: &mut Context<Self>) -> impl IntoElement {
        self.rendered_frames.set(self.rendered_frames.get() + 1);
        let colors = Colors::for_appearance(window);
        let messages = Rc::clone(&self.messages);
        let rendered_items = Rc::clone(&self.rendered_items);

        div()
            .size_full()
            .bg(colors.background)
            .text_color(colors.text)
            .child(
                list(self.list_state.clone(), move |ix, _window, _cx| {
                    rendered_items.set(rendered_items.get() + 1);
                    render_message_row(&messages[ix], &colors)
                })
                .size_full(),
            )
    }
}

fn render_message_row(msg: &SyntheticMessage, colors: &Colors) -> AnyElement {
    let bubble_bg = if msg.outgoing {
        colors.selected
    } else {
        colors.container
    };
    let text_color = if msg.outgoing {
        colors.selected_text
    } else {
        colors.text
    };
    let highlight = HighlightStyle {
        color: Some(rgb_to_hsla(colors.disabled)),
        ..Default::default()
    };

    div()
        .w_full()
        .px_4()
        .py_1()
        .flex()
        .when(msg.outgoing, |el| el.justify_end())
        .when(!msg.outgoing, |el| el.justify_start())
        .child(
            div()
                .max_w(px(520.0))
                .px_3()
                .py_2()
                .rounded(px(design_tokens::LUXURY_WATCHER_CARD_RADIUS))
                .bg(bubble_bg)
                .border_1()
                .border_color(colors.border)
                .flex()
                .flex_col()
                .gap_1()
                .child(
                    div()
                        .text_xs()
                        .text_color(colors.disabled)
                        .child(msg.sender.clone()),
                )
                .child(
                    div().text_sm().text_color(text_color).child(
                        StyledText::new(msg.body.clone())
                            .with_highlights([(0..msg.highlight_len, highlight)]),
                    ),
                )
                .child(
                    div()
                        .flex()
                        .justify_end()
                        .text_xs()
                        .text_color(colors.disabled)
                        .child(msg.timestamp.clone()),
                ),
        )
        .into_any_element()
}

fn build_synthetic_messages(count: usize) -> Vec<SyntheticMessage> {
    const SENDERS: [&str; 6] = [
        "Alice",
        "Bob",
        "Charlie",
        "LuxuryGram Channel",
        "Diana",
        "Eve",
    ];
    const SNIPPETS: [&str; 5] = [
        "Short ping check.",
        "Benchmarks need deterministic text runs, wrapping across multiple lines when the bubble width hits 520px.",
        "MTProto update stream + virtualized GPUI-CE list scroll baseline under 10k-100k history items.",
        "Checking variable-height message bubbles with sender header, styled entity prefix, and trailing timestamp.",
        "https://luxurygram.example/thread/42 — longer URL and mixed Cyrillic/Latin check: Привет, тест рендера 120 FPS.",
    ];

    (0..count)
        .map(|ix| {
            let sender = SharedString::from(SENDERS[ix % SENDERS.len()]);
            let snippet = SNIPPETS[ix % SNIPPETS.len()];
            let body = SharedString::from(format!("#{ix:06} {snippet}"));
            let highlight_len = body.find(' ').unwrap_or(0);
            let timestamp = SharedString::from(format!("{:02}:{:02}", (ix / 60) % 24, ix % 60));
            SyntheticMessage {
                sender,
                body,
                highlight_len,
                timestamp,
                outgoing: ix % 3 == 0,
            }
        })
        .collect()
}

#[gpui::bench(
    inputs = [
        ScrollBenchCase { message_count: 10_000, scroll_step_px: 48.0 },
        ScrollBenchCase { message_count: 100_000, scroll_step_px: 48.0 },
        ScrollBenchCase { message_count: 100_000, scroll_step_px: 480.0 },
    ],
    input_name = "case",
    group = "message_list_scroll",
    fps = 120
)]
fn scroll_feed(input: &ScrollBenchCase, cx: &mut BenchAppContext) {
    if cfg!(debug_assertions) {
        panic!("benchmarks require release mode");
    }

    let mut window = cx.add_empty_window();
    let feed = window.update(|window, cx| {
        window.replace_root(cx, |_, _| {
            MessageFeed::new(input.message_count, input.scroll_step_px)
        })
    });
    cx.run_until_idle();

    let (initial_frames, initial_items) = feed.read_with(cx, |view, _| {
        (view.rendered_frames.get(), view.rendered_items.get())
    });
    assert!(initial_frames > 0, "expected initial frame to render");
    assert!(
        initial_items > 0 && initial_items < 200,
        "expected virtualized list to render only viewport+overdraw items, got {initial_items}"
    );
    feed.read_with(cx, |view, _| {
        view.rendered_frames.set(0);
        view.rendered_items.set(0);
    });

    let mut update_calls = 0usize;
    cx.bench_renderer(feed.clone(), |view, _window, cx| {
        view.step_scroll();
        update_calls += 1;
        cx.notify();
    });

    let (rendered_frames, rendered_items) = feed.read_with(cx, |view, _| {
        (view.rendered_frames.get(), view.rendered_items.get())
    });
    assert!(update_calls > 0);
    assert_eq!(rendered_frames, update_calls);
    assert!(
        rendered_items > 0,
        "expected scrolled frames to render visible items"
    );
    let avg_items_per_frame = rendered_items as f64 / rendered_frames as f64;
    assert!(
        avg_items_per_frame < 200.0,
        "virtualization failed: averaged {avg_items_per_frame:.1} items/frame out of {}",
        input.message_count
    );
}

/// Measures the shipped `luxurygram_shell::feed::MessageFeed` so H1's new view
/// is compared with the H0 baseline above under the same scroll workload.
#[gpui::bench(
    inputs = [
        ScrollBenchCase { message_count: 10_000, scroll_step_px: 48.0 },
        ScrollBenchCase { message_count: 100_000, scroll_step_px: 48.0 },
        ScrollBenchCase { message_count: 100_000, scroll_step_px: 480.0 },
    ],
    input_name = "case",
    group = "message_list_scroll_production",
    fps = 120
)]
fn scroll_feed_production(input: &ScrollBenchCase, cx: &mut BenchAppContext) {
    if cfg!(debug_assertions) {
        panic!("benchmarks require release mode");
    }

    let mut window = cx.add_empty_window();
    let feed = window.update(|window, cx| {
        window.replace_root(cx, |_, cx| {
            ProductionFeed::new(input.message_count, Rc::new(Cell::new(0)), cx)
        })
    });
    cx.run_until_idle();

    let initial_items = feed.read_with(cx, |view, _| view.rendered_items());
    assert!(
        initial_items > 0,
        "production feed rendered no rows initially"
    );
    feed.read_with(cx, |view, _| view.reset_render_counter());

    let mut steps = 0usize;
    let mut total_steps = 0usize;
    let mut direction = -1.0f32;
    cx.bench_renderer(feed.clone(), |view, _window, cx| {
        if steps >= 200 {
            direction = -direction;
            steps = 0;
        }
        view.scroll_by(input.scroll_step_px * direction);
        steps += 1;
        total_steps += 1;
        cx.notify();
    });

    let items = feed.read_with(cx, |view, _| view.rendered_items());
    assert!(total_steps > 0 && items > 0);
    let avg_items_per_frame = items as f64 / total_steps as f64;
    assert!(
        avg_items_per_frame < 200.0,
        "virtualization failed: averaged {avg_items_per_frame:.1} items/frame out of {}",
        input.message_count
    );
}

gpui::bench_group! {
    name = benches;
    config = criterion::Criterion::default()
        .sample_size(20)
        .warm_up_time(Duration::from_secs(1))
        .measurement_time(Duration::from_secs(3))
        .without_plots();
    targets = scroll_feed, scroll_feed_production
}
gpui::bench_main!(benches);
