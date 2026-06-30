/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

//! Usage: `gcab --create OUTPUT.cab INPUT1 [INPUT2 ...]`

use fwupd::cab::{CabArchive, CabArchiveFile, CabFileData};
use std::process::ExitCode;
use std::{env, fs, path::Path};

fn run() -> Result<(), String> {
    let args: Vec<String> = env::args().collect();

    if args.len() < 4 || (args[1] != "--create" && args[1] != "-c") {
        return Err(format!(
            "Usage: {} --create OUTPUT.cab INPUT [INPUT ...]",
            args[0]
        ));
    }

    let output = &args[2];
    let inputs = &args[3..];

    let mut files = Vec::new();
    for path in inputs {
        let data = fs::read(path).map_err(|e| format!("Failed to load file {path}: {e}"))?;
        let basename = Path::new(path)
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or(path);

        files.push(CabArchiveFile {
            name: basename.to_string(),
            data: CabFileData::Owned(data),
            ..Default::default()
        });
    }

    let archive = CabArchive {
        files,
        is_compressed: false,
    };

    let cab_data = archive
        .write()
        .map_err(|e| format!("Failed to write CAB: {e}"))?;
    fs::write(output, cab_data).map_err(|e| format!("Failed to write file {output}: {e}"))?;

    Ok(())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(msg) => {
            eprintln!("{msg}");
            ExitCode::FAILURE
        }
    }
}
