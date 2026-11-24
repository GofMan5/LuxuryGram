//! Rust port of LuxuryGram's hot UI and tooling slices.
//!
//! The binary `luxurygram-shell` provides the runnable GPUI-CE shell; this
//! library exposes the same modules to benchmarks so they measure the shipped
//! view rather than a copy of it.

pub mod feed;
pub mod message;

#[cfg(all(windows, feature = "windows-host-smoke"))]
mod windows_host_smoke {
    /// Test only: run GPUI on a thread owned by the native C++ host.
    // SAFETY: This opt-in DLL owns the unique export name. No data or Rust
    // objects cross the ABI, and unwinding is contained here.
    #[unsafe(no_mangle)]
    pub extern "C" fn luxurygram_gpui_windows_host_smoke() -> i32 {
        match std::panic::catch_unwind(|| {
            gpui_platform::application()
                .with_quit_mode(gpui::QuitMode::LastWindowClosed)
                .run(|cx| crate::feed::run(cx, None));
        }) {
            Ok(()) => 0,
            Err(_) => 1,
        }
    }
}
