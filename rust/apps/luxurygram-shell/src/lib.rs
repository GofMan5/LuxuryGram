//! Rust port of LuxuryGram's hot UI and tooling slices.
//!
//! The binary `luxurygram-shell` provides the runnable GPUI-CE shell; this
//! library exposes the same modules to benchmarks so they measure the shipped
//! view rather than a copy of it.

pub mod feed;
pub mod message;

#[cfg(all(windows, feature = "windows-host-smoke"))]
mod windows_host_smoke {
    use crate::message::{Message, MessageId, MessageStore};

    /// Test only: open a feed with one row supplied by the native host.
    ///
    /// # Safety
    /// Both pointers must reference their stated number of initialized bytes
    /// and remain readable without mutation until this function returns.
    // SAFETY: The opt-in test DLL is the sole owner of this export name.
    #[unsafe(no_mangle)]
    pub unsafe extern "C" fn luxurygram_gpui_windows_host_smoke_v2(
        peer: u64,
        message: i64,
        timestamp: u64,
        sender: *const u8,
        sender_len: usize,
        text: *const u8,
        text_len: usize,
    ) -> i32 {
        if sender.is_null() || text.is_null() || sender_len > 4096 || text_len > 65536 {
            return 2;
        }
        std::panic::catch_unwind(|| {
            // SAFETY: The host guarantees readability; sizes are bounded above.
            let sender = unsafe { std::slice::from_raw_parts(sender, sender_len) };
            let text = unsafe { std::slice::from_raw_parts(text, text_len) };
            let (Ok(sender), Ok(text)) = (std::str::from_utf8(sender), std::str::from_utf8(text))
            else {
                return 2;
            };
            let store = MessageStore::new(vec![Message {
                id: MessageId { peer, message },
                sender: sender.to_owned(),
                text: text.to_owned(),
                entities: Vec::new(),
                timestamp,
                outgoing: false,
                date_label: String::new(),
                show_date_header: false,
                first_in_group: true,
            }]);
            gpui_platform::application()
                .with_quit_mode(gpui::QuitMode::LastWindowClosed)
                .run(move |cx| {
                    crate::feed::open_feed(cx, Some(store), "LuxuryGram — Native history test")
                });
            0
        })
        .unwrap_or(1)
    }
}
