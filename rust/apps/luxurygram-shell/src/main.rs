//! M0 shell for the Rust LuxuryGram port.
//!
//! API verified against the pinned gpui-ce 0.3.0 source and examples:
//! `examples/hello_world.rs` and `src/platform.rs` (`WindowOptions`,
//! `TitlebarOptions`) in the crates.io package.

use gpui::{
    App, Bounds, Context, QuitMode, TitlebarOptions, Window, WindowBounds, WindowOptions,
    colors::Colors, div, prelude::*, px, size,
};

struct Shell;

impl Render for Shell {
    fn render(&mut self, window: &mut Window, _cx: &mut Context<Self>) -> impl IntoElement {
        let colors = Colors::for_appearance(window);

        div()
            .size_full()
            .flex()
            .flex_col()
            .items_center()
            .justify_center()
            .gap_2()
            .bg(colors.background)
            .text_color(colors.text)
            .child("LuxuryGram (Rust port — M0)")
            .child(
                div()
                    .text_color(colors.disabled)
                    .child("Window shell only; no Telegram functionality yet."),
            )
    }
}

fn main() {
    gpui_platform::application()
        .with_quit_mode(QuitMode::LastWindowClosed)
        .run(|cx: &mut App| {
            let bounds = Bounds::centered(None, size(px(960.), px(640.)), cx);
            cx.open_window(
                WindowOptions::default()
                    .window_bounds(WindowBounds::Windowed(bounds))
                    .titlebar(TitlebarOptions {
                        title: Some("LuxuryGram".into()),
                        ..Default::default()
                    }),
                |_window, cx| cx.new(|_cx| Shell),
            )
            .expect("open the main window");
            cx.activate(true);
        });
}
