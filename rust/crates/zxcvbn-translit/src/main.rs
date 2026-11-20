// This file is part of Telegram Desktop,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

use std::env;
use std::ffi::OsString;
use std::fs::{self, File};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use zxcvbn_translit::{TransformError, transform};

fn fail(message: String) -> ExitCode {
    eprintln!("zxcvbn_translit: {message}");
    ExitCode::from(1)
}

fn main() -> ExitCode {
    let args: Vec<OsString> = env::args_os().collect();
    if args.len() != 3 {
        eprintln!("usage: zxcvbn_translit <input.txt> <output.txt>");
        return ExitCode::from(1);
    }

    let input_path = Path::new(&args[1]);
    let output_path = Path::new(&args[2]);

    let input = match fs::read(input_path) {
        Ok(bytes) => bytes,
        Err(_) => return fail(format!("cannot read {}", input_path.display())),
    };

    let mut temporary = output_path.as_os_str().to_os_string();
    temporary.push(".tmp");
    let temporary = PathBuf::from(temporary);
    let _ = fs::remove_file(&temporary);

    let mut output = match File::create(&temporary) {
        Ok(file) => file,
        Err(_) => return fail(format!("cannot write {}", output_path.display())),
    };

    let bytes = match transform(&input) {
        Ok(bytes) => bytes,
        Err(TransformError::NoWordsMapped) => {
            drop(output);
            let _ = fs::remove_file(&temporary);
            return fail(format!("no words mapped from {}", input_path.display()));
        }
    };

    if output.write_all(&bytes).is_err() || output.flush().is_err() {
        drop(output);
        let _ = fs::remove_file(&temporary);
        return fail(format!("cannot write {}", output_path.display()));
    }
    drop(output);

    let _ = fs::remove_file(output_path);
    if fs::rename(&temporary, output_path).is_err() {
        let _ = fs::remove_file(&temporary);
        return fail(format!("cannot write {}", output_path.display()));
    }

    ExitCode::SUCCESS
}
