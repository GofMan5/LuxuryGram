//! H1 UI slice for the Rust LuxuryGram port.
//!
//! The virtualized message feed is the first bottleneck slice from ADR-010:
//! GPUI-CE `list()` with bottom alignment and tail following replaces the
//! QPainter-backed history widget for the measured scroll path.

mod feed;
mod message;

use gpui::QuitMode;

fn main() {
    gpui_platform::application()
        .with_quit_mode(QuitMode::LastWindowClosed)
        .run(feed::run);
}
