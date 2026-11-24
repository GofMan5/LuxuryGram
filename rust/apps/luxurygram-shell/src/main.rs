//! H1 UI slice for the Rust LuxuryGram port.
//!
//! The virtualized message feed is the first bottleneck slice from ADR-010:
//! standalone GPUI-CE feed can open a single-chat JSON snapshot offline.

mod history_json;

use std::{env, path::Path, process::ExitCode};

use gpui::QuitMode;
use luxurygram_shell::message::MessageStore;

fn main() -> ExitCode {
    let args: Vec<_> = env::args_os().skip(1).collect();
    let mut compose_preview = false;
    let mut rest: Vec<_> = Vec::new();
    for arg in args {
        if arg == "--compose-preview" {
            compose_preview = true;
        } else {
            rest.push(arg);
        }
    }
    let store = match rest.as_slice() {
        [] => None,
        [flag] if flag == "--help" => {
            println!("Usage: luxurygram-shell [--compose-preview] [--history-json <result.json>]");
            return ExitCode::SUCCESS;
        }
        [flag, path] if flag == "--history-json" => match history_json::load(Path::new(path)) {
            Ok(messages) => Some(MessageStore::new(messages)),
            Err(error) => {
                eprintln!("Cannot open chat export: {error}");
                return ExitCode::FAILURE;
            }
        },
        _ => {
            eprintln!("Usage: luxurygram-shell [--compose-preview] [--history-json <result.json>]");
            return ExitCode::FAILURE;
        }
    };
    gpui_platform::application()
        .with_quit_mode(QuitMode::LastWindowClosed)
        .run(move |cx| {
            if compose_preview {
                luxurygram_shell::feed::run_with_compose(
                    cx,
                    store,
                    Some(Box::new(|text| {
                        println!("sent: {text}");
                    })),
                );
            } else {
                luxurygram_shell::feed::run(cx, store);
            }
        });
    ExitCode::SUCCESS
}
