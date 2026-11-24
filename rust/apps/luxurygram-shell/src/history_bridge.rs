//! C ABI between the native client (C++) and the GPUI history feed.
//!
//! The native side owns the model, the session and the subscription
//! (`Data::HistoryTextViewer`). This bridge only carries an owned snapshot
//! across the boundary and never touches the network, storage or a profile.
//!
//! Threading: GPUI on Windows owns a Win32 message loop, so the feed runs on
//! its own thread. Any thread posts into a channel; a task on the GPUI thread
//! drains it. A revoked subscription (lock, logout, account or chat switch)
//! is [`clear`], which also drops every snapshot already queued.

use std::sync::Mutex;
use std::sync::atomic::{AtomicIsize, Ordering};
use std::sync::mpsc;

use raw_window_handle::{HasWindowHandle, RawWindowHandle};

use crate::message::{Message, MessageId, MessageStore};

/// One row of a snapshot. Mirrors `Data::HistoryTextRow`.
#[repr(C)]
pub struct HistoryRowAbi {
    pub peer: u64,
    pub message: i64,
    pub date: u32,
    pub outgoing: u8,
    pub sender: *const u8,
    pub sender_len: usize,
    pub text: *const u8,
    pub text_len: usize,
}

/// One ID change since the previous snapshot, in event order. Mirrors
/// `Data::HistoryTextRekey`.
#[repr(C)]
pub struct HistoryRekeyAbi {
    pub old_peer: u64,
    pub old_message: i64,
    pub new_peer: u64,
    pub new_message: i64,
}

enum Command {
    Apply(Vec<Message>, Vec<(MessageId, MessageId)>),
    Clear,
    /// Show or hide the composer: a send callback is installed or removed.
    Composer(bool),
}

static SENDER: Mutex<Option<mpsc::Sender<Command>>> = Mutex::new(None);
static VISIBLE_COUNT: AtomicIsize = AtomicIsize::new(0);
// The Win32 window of the feed, or 0 while no feed window exists. The native
// host reads it once to place the window over its own chat rectangle.
static WINDOW: AtomicIsize = AtomicIsize::new(0);
const MAX_ROWS: usize = 100_000;
const MAX_TEXT: usize = 65_536;
const MAX_SENDER: usize = 4_096;
const POLL: std::time::Duration = std::time::Duration::from_millis(33);

/// The feed window's HWND, or 0 when the feed is not running.
///
/// The handle stays valid until the window closes; the bridge clears it then.
#[unsafe(no_mangle)]
pub extern "C" fn luxurygram_history_window() -> isize {
    WINDOW.load(Ordering::SeqCst)
}

/// Called on the GPUI thread with UTF-8 text the user submitted from the
/// composer. `context` is the pointer passed to
/// `luxurygram_history_set_send_callback`. The callee must marshal to its own
/// thread; the text is only valid for the duration of the call. Return nonzero
/// when the text was accepted, zero to keep it in the composer.
pub type SendCallback =
    unsafe extern "C" fn(context: *mut std::ffi::c_void, text: *const u8, text_len: usize) -> i32;

static SEND_CALLBACK: Mutex<Option<(SendCallback, usize)>> = Mutex::new(None);

/// Route composer submissions to the native send path.
///
/// Must be called before `luxurygram_history_start` for the composer to accept
/// text at all; the feed stays read-only while no callback is set. Passing a
/// null callback removes it again. The callback is invoked on the GPUI thread.
///
/// # Safety
/// `context` must stay valid until the callback is replaced or removed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn luxurygram_history_set_send_callback(
    callback: Option<SendCallback>,
    context: *mut std::ffi::c_void,
) -> i32 {
    let mut slot = SEND_CALLBACK.lock().unwrap();
    *slot = callback.map(|callback| (callback, context as usize));
    let installed = slot.is_some();
    drop(slot);
    // Tell the feed on its own thread, if it is running. A feed that starts
    // later reads the slot itself, so a missing or closed channel is fine.
    send(Command::Composer(installed));
    if installed { 0 } else { 1 }
}

/// The feed's send handler: forwards the submitted text through the callback
/// that is installed at call time, so a late `set_send_callback` takes effect.
/// Returns `false` when no callback is installed, so the feed keeps the draft.
fn forward_send(text: String) -> bool {
    let slot = SEND_CALLBACK.lock().unwrap();
    let Some((callback, context)) = *slot else {
        return false;
    };
    // SAFETY: the caller of `set_send_callback` guarantees `context` is alive
    // and the callback reads `text` only during the call.
    unsafe { callback(context as *mut std::ffi::c_void, text.as_ptr(), text.len()) != 0 }
}

/// Start the GPUI feed on its own thread and accept snapshots afterwards.
///
/// Blocks until the feed is ready, so a caller may post right after return.
#[unsafe(no_mangle)]
pub extern "C" fn luxurygram_history_start() -> i32 {
    let (sender, receiver) = mpsc::channel();
    {
        let mut slot = SENDER.lock().unwrap();
        if slot.is_some() {
            return 1;
        }
        *slot = Some(sender);
    }
    VISIBLE_COUNT.store(0, Ordering::SeqCst);
    let spawned = std::thread::Builder::new()
        .name("luxurygram-gpui".into())
        .spawn(move || {
            gpui_platform::application()
                .with_quit_mode(gpui::QuitMode::LastWindowClosed)
                .run(move |cx| {
                    let (feed, window) = crate::feed::open_feed_window(
                        cx,
                        Some(MessageStore::default()),
                        "LuxuryGram — History",
                    );
                    let composer_installed = SEND_CALLBACK.lock().unwrap().is_some();
                    feed.update(cx, |feed, cx| {
                        feed.set_send_handler(
                            composer_installed.then(|| std::rc::Rc::new(forward_send) as _),
                            cx,
                        );
                    });
                    publish_window_handle(&window, cx);
                    let subscription = feed.update(cx, |feed, cx| feed.begin_history(cx));
                    VISIBLE_COUNT.store(0, Ordering::SeqCst);
                    cx.spawn(async move |cx| {
                        loop {
                            cx.background_executor().timer(POLL).await;
                            let mut pending: Vec<Command> = Vec::new();
                            while let Ok(command) = receiver.try_recv() {
                                if matches!(command, Command::Clear) {
                                    pending.clear();
                                }
                                pending.push(command);
                            }
                            for command in pending {
                                match command {
                                    Command::Apply(rows, rekeys) => {
                                        let count = rows.len();
                                        feed.update(cx, |feed, cx| {
                                            feed.update_history(
                                                &subscription,
                                                MessageStore::new(rows),
                                                &rekeys,
                                                cx,
                                            );
                                        });
                                        VISIBLE_COUNT.store(count as isize, Ordering::SeqCst);
                                    }
                                    Command::Clear => {
                                        feed.update(cx, |feed, cx| feed.clear_history(cx));
                                        VISIBLE_COUNT.store(0, Ordering::SeqCst);
                                    }
                                    Command::Composer(installed) => {
                                        feed.update(cx, |feed, cx| {
                                            feed.set_send_handler(
                                                installed
                                                    .then(|| std::rc::Rc::new(forward_send) as _),
                                                cx,
                                            );
                                        });
                                    }
                                }
                            }
                        }
                    })
                    .detach();
                });
            *SENDER.lock().unwrap() = None;
            WINDOW.store(0, Ordering::SeqCst);
        });
    match spawned {
        Ok(_) => 0,
        Err(_) => {
            *SENDER.lock().unwrap() = None;
            1
        }
    }
}

/// Publish the feed window's native handle for the host.
///
/// Runs on the GPUI thread right after the window opens, so the handle is
/// valid before `luxurygram_history_start` reports the feed as running.
fn publish_window_handle(
    window: &gpui::WindowHandle<crate::feed::MessageFeed>,
    cx: &mut gpui::App,
) {
    let handle = window.update(cx, |_, window, _| {
        window
            .window_handle()
            .ok()
            .and_then(|handle| match handle.as_raw() {
                RawWindowHandle::Win32(win32) => Some(win32.hwnd.get()),
                _ => None,
            })
    });
    WINDOW.store(handle.ok().flatten().unwrap_or(0), Ordering::SeqCst);
}

/// Read a UTF-8 slice supplied by the native caller.
///
/// # Safety
/// `ptr` must reference `len` initialized bytes for the duration of the call.
unsafe fn read_str(ptr: *const u8, len: usize, max: usize) -> Result<String, i32> {
    if ptr.is_null() || len > max {
        return Err(2);
    }
    // SAFETY: The caller guarantees readability; `len` is bounded by `max`.
    let bytes = unsafe { std::slice::from_raw_parts(ptr, len) };
    std::str::from_utf8(bytes).map(str::to_owned).map_err(|_| 2)
}

/// Apply an owned snapshot to the live GPUI feed from any thread.
///
/// `rows` must be ordered oldest-to-newest with unique full IDs. `rekeys` are
/// applied in event order to keep the scroll anchor across the replacement.
/// Returns 0 when queued, 1 when the feed is not running, 2 on bad input.
///
/// # Safety
/// Both arrays and every string pointer must reference initialized memory for
/// the duration of the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn luxurygram_history_apply(
    rows: *const HistoryRowAbi,
    rows_len: usize,
    rekeys: *const HistoryRekeyAbi,
    rekeys_len: usize,
) -> i32 {
    if rows_len > MAX_ROWS || rekeys_len > MAX_ROWS || (rows.is_null() && rows_len != 0) {
        return 2;
    }
    let mut messages = Vec::with_capacity(rows_len);
    for index in 0..rows_len {
        // SAFETY: The caller guarantees `rows_len` valid elements.
        let row = unsafe { &*rows.add(index) };
        let (Ok(sender), Ok(text)) = (
            unsafe { read_str(row.sender, row.sender_len, MAX_SENDER) },
            unsafe { read_str(row.text, row.text_len, MAX_TEXT) },
        ) else {
            return 2;
        };
        messages.push(Message {
            id: MessageId {
                peer: row.peer,
                message: row.message,
            },
            sender,
            text,
            entities: Vec::new(),
            timestamp: u64::from(row.date),
            outgoing: row.outgoing != 0,
            date_label: String::new(),
            show_date_header: false,
            first_in_group: true,
        });
    }
    let mut rekeyed = Vec::with_capacity(rekeys_len);
    for index in 0..rekeys_len {
        // SAFETY: The caller guarantees `rekeys_len` valid elements.
        let rekey = unsafe { &*rekeys.add(index) };
        rekeyed.push((
            MessageId {
                peer: rekey.old_peer,
                message: rekey.old_message,
            },
            MessageId {
                peer: rekey.new_peer,
                message: rekey.new_message,
            },
        ));
    }
    send(Command::Apply(messages, rekeyed))
}

/// Revoke the visible history on lock, logout, account switch or chat switch.
#[unsafe(no_mangle)]
pub extern "C" fn luxurygram_history_clear() -> i32 {
    send(Command::Clear)
}

/// Number of rows the feed applied last, for checks and diagnostics.
#[unsafe(no_mangle)]
pub extern "C" fn luxurygram_history_visible_count() -> isize {
    VISIBLE_COUNT.load(Ordering::SeqCst)
}

fn send(command: Command) -> i32 {
    let sender = SENDER.lock().unwrap();
    match sender.as_ref() {
        Some(sender) if sender.send(command).is_ok() => 0,
        _ => 1,
    }
}
