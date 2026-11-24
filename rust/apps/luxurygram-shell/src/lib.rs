//! Rust port of LuxuryGram's hot UI and tooling slices.
//!
//! The binary `luxurygram-shell` provides the runnable GPUI-CE shell; this
//! library exposes the same modules to benchmarks so they measure the shipped
//! view rather than a copy of it.

pub mod dialogs;
pub mod feed;
#[cfg(windows)]
pub mod history_bridge;
pub mod message;
