/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

use std::fs::File;
use std::io::{BufRead, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::{Duration, UNIX_EPOCH};

use clap::{Parser, Subcommand};
use fwupd::cab::wire::{
    CFData, CFFile, CFFolder, CFHeader, CFHeaderFlag, CFHeaderFlags, CFHeaderReserve, FixedSizeIO,
};
use fwupd::cab::{
    Archive, ArchiveBuilder, Built, CabPath, Checksum, FileBuilder, MsDosDate, MsDosTime,
    ParserFlag, ParserFlags,
};
use fwupd::streams::{FileInputStream, IntoStream, ReadSeek};
use fwupd::units::ByteCount;
use fwupd::Bitflags;

#[derive(Parser)]
#[command(name = "cabtool", about = "Inspect and extract CAB archives")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// List files inside a CAB archive
    List {
        /// CAB archive to inspect
        file: PathBuf,

        /// Long listing: show size, date, and time
        #[arg(short = 'l', long)]
        long: bool,

        /// With -l, show sizes in human-readable form (e.g. 2.9KB)
        ///
        // FIXME: can't use short -l here because it conflicts with --help
        #[arg(long, requires = "long")]
        human_readable: bool,

        /// Ignore checksums
        #[arg(long)]
        ignore_checksum: bool,
    },
    /// Extract files from a CAB archive
    Extract {
        /// CAB archive to extract
        file: PathBuf,

        /// Directory to extract into (default: current directory)
        #[arg(short, long, default_value = ".")]
        output: PathBuf,

        /// Ignore checksums
        #[arg(long)]
        ignore_checksum: bool,

        /// Strip directory components, use only the filename
        #[arg(long)]
        only_basename: bool,
    },
    /// Show wire-level CAB structure
    Show {
        /// CAB archive to inspect
        file: PathBuf,

        /// Decompress and print each file's payload (text for txt/json/xml, hex otherwise)
        #[arg(long)]
        show_payload: bool,
    },
    /// Create a new CAB archive from files
    Create {
        /// Output CAB archive path
        output: PathBuf,

        /// Input files to pack into the archive
        #[arg(required = true)]
        files: Vec<PathBuf>,
    },
}

fn open_archive(path: &Path, flags: ParserFlags) -> Result<Archive, String> {
    let file = File::open(path).map_err(|e| format!("cannot open {}: {e}", path.display()))?;
    let src: Arc<Mutex<dyn ReadSeek + Send>> = file.into_stream();
    Archive::new(src, 0, flags).map_err(|e| format!("{e}"))
}

fn cmd_list(
    path: &Path,
    flags: ParserFlags,
    long: bool,
    human_readable: bool,
) -> Result<(), String> {
    let archive = open_archive(path, flags)?;

    if !long {
        for entry in archive.files() {
            println!("{}", entry.name());
        }
        return Ok(());
    }

    let size_width = archive
        .files()
        .iter()
        .map(|e| {
            if human_readable {
                ByteCount::from(e.size()).to_string().len()
            } else {
                e.size().to_string().len()
            }
        })
        .max()
        .unwrap_or(1);

    for entry in archive.files() {
        let size_str = if human_readable {
            ByteCount::from(entry.size()).to_string()
        } else {
            entry.size().to_string()
        };
        let d = entry.date().unwrap_or(MsDosDate::from(0));
        let t = entry.time();
        println!(
            "{:>width$}  {:04}-{:02}-{:02} {:02}:{:02}  {}",
            size_str,
            d.year(),
            d.month(),
            d.day(),
            t.hour(),
            t.minute(),
            entry.name(),
            width = size_width,
        );
    }
    Ok(())
}

fn open_input(path: &Path) -> Result<FileBuilder<Built>, String> {
    let file = std::fs::File::open(path)
        .map_err(|e| format!("Failed to open '{}': {e}", path.display()))?;
    let meta = file
        .metadata()
        .map_err(|e| format!("cannot stat {}: {e}", path.display()))?;
    let stream = FileInputStream::from_file(file).into_stream();
    let size = meta.len();
    let name = path
        .file_name()
        .ok_or_else(|| format!("no filename in path: {}", path.display()))?
        .to_string_lossy();
    let cab_path: CabPath = name
        .parse()
        .map_err(|e| format!("invalid filename '{name}': {e}"))?;
    let (date, time) = meta.modified().map_or(
        (MsDosDate::from(0), MsDosTime::from(0)),
        system_time_to_msdos,
    );

    Ok(FileBuilder::new(cab_path, stream, size)
        .date(date)
        .time(time)
        .build())
}

fn cmd_create(output: &Path, inputs: &[PathBuf]) -> Result<(), String> {
    let (first, rest) = inputs.split_first().ok_or("no input files")?;

    let file = open_input(first)?;
    let mut builder = ArchiveBuilder::new().add_file(file);

    for input in rest {
        let file = open_input(input)?;
        builder = builder.add_file(file);
    }

    let out = std::fs::File::create(output)
        .map_err(|e| format!("cannot create {}: {e}", output.display()))?;
    builder
        .build(std::io::BufWriter::new(out))
        .map_err(|e| format!("build failed: {e}"))?;
    eprintln!("created: {}", output.display());
    Ok(())
}

// Convert seconds since epoch from `SystemTime` to calendar fields without external crates.
// Using the civil-time algorithm by Howard Hinnant,
// https://howardhinnant.github.io/date_algorithms.html
#[allow(clippy::many_single_char_names)]
fn system_time_to_msdos(t: std::time::SystemTime) -> (MsDosDate, MsDosTime) {
    // Safe to unwrap the various fields, we're within expected ranges
    const SECS_PER_DAY: u64 = 60 * 60 * 24;
    const DAYS_PER_ERA: i64 = 146_097; // an era is a 400y span, internal to the algorithm
    const EPOCH_SHIFT: i64 = 719_468; // shifts the epoch from 1970-01-01 to 0000-03-01 because
                                      // years start 01 March in this algorithm.

    let secs = t
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_secs();

    let z = i64::try_from(secs / SECS_PER_DAY).unwrap() + EPOCH_SHIFT;
    // For us, era should be 4 (1600 to 2000) or 5 (2000 to 2400)
    // In this algorithm, years start 01 March.
    let era = z.div_euclid(DAYS_PER_ERA);

    // doe = day_of_era, yoe = year of era, etc.
    let doe = u32::try_from(z - era * DAYS_PER_ERA).unwrap();
    // 1460 - last day in year 3
    // 36524 - days in the first 100 years of the era
    let yoe =
        (doe - doe / 1460 + doe / 36524 - doe / u32::try_from(DAYS_PER_ERA - 1).unwrap()) / 365;
    let y = i64::from(yoe) + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = if m <= 2 { y + 1 } else { y };

    let d = u8::try_from(d).unwrap();
    let m = u8::try_from(m).unwrap();
    let y = u16::try_from(y.clamp(1980, 2107)).unwrap();

    let h = (secs % 86400 / 3600) as u8;
    let min = (secs % 3600 / 60) as u8;
    let sec = (secs % 60) as u8;
    (
        MsDosDate::from_ymd(y, m, d),
        MsDosTime::from_hms(h, min, sec),
    )
}

fn cmd_extract(path: &Path, output: &Path, flags: ParserFlags) -> Result<(), String> {
    let mut archive = open_archive(path, flags)?;
    std::fs::create_dir_all(output).map_err(|e| format!("cannot create output directory: {e}"))?;

    for entry in archive.files_mut() {
        let dest = output.join(entry.name().path().to_string_lossy().as_ref());
        if let Some(parent) = dest.parent() {
            std::fs::create_dir_all(parent)
                .map_err(|e| format!("cannot create {}: {e}", parent.display()))?;
        }
        let mut out =
            File::create(&dest).map_err(|e| format!("cannot create {}: {e}", dest.display()))?;
        let mut buf = Vec::new();
        entry
            .read_to_end(&mut buf)
            .map_err(|e| format!("read error for {}: {e}", entry.name()))?;
        out.write_all(&buf)
            .map_err(|e| format!("write error for {}: {e}", dest.display()))?;
        eprintln!("extracted: {}", dest.display());
    }
    Ok(())
}

fn read_nul_string(src: &mut impl ReadSeek, offset: usize) -> Result<(String, usize), String> {
    src.seek(SeekFrom::Start(offset as u64))
        .map_err(|e| format!("seek failed: {e}"))?;
    let mut buf = Vec::new();
    let mut reader = std::io::BufReader::new(&mut *src);
    reader
        .read_until(0, &mut buf)
        .map_err(|e| format!("read failed: {e}"))?;
    let len = buf.len();
    let s = String::from_utf8_lossy(buf.strip_suffix(&[0]).unwrap_or(&buf)).into_owned();
    Ok((s, len))
}

fn read_at(file: &mut File, offset: usize, count: usize) -> Result<Vec<u8>, String> {
    file.seek(SeekFrom::Start(offset as u64))
        .map_err(|e| format!("seek to 0x{offset:x}: {e}"))?;
    let mut buf = vec![0u8; count];
    file.read_exact(&mut buf)
        .map_err(|e| format!("read {count} bytes at 0x{offset:x}: {e}"))?;
    Ok(buf)
}

/// Format bytes as hex, showing at most `max` bytes; if longer, show first
/// `max` bytes then ".." then the final byte.
fn hex_abbrev(bytes: &[u8], max: usize) -> String {
    if bytes.len() <= max {
        bytes
            .iter()
            .map(|b| format!("{b:02x}"))
            .collect::<Vec<_>>()
            .join(" ")
    } else {
        let head = bytes[..max]
            .iter()
            .map(|b| format!("{b:02x}"))
            .collect::<Vec<_>>()
            .join(" ");
        format!("{head} .. {:02x}", bytes[bytes.len() - 1])
    }
}

fn is_text_extension(name: &str) -> bool {
    matches!(
        Path::new(name)
            .extension()
            .and_then(|e| e.to_str())
            .map(str::to_ascii_lowercase)
            .as_deref(),
        Some("txt" | "json" | "xml")
    )
}

fn print_hexdump(data: &[u8]) {
    for (i, chunk) in data.chunks(16).enumerate() {
        let offset = i * 16;
        print!("{offset:08x}  ");
        for (j, &b) in chunk.iter().enumerate() {
            if j == 8 {
                print!(" ");
            }
            print!("{b:02x} ");
        }
        for j in chunk.len()..16 {
            if j == 8 {
                print!(" ");
            }
            print!("   ");
        }
        print!("|");
        for &b in chunk {
            if b.is_ascii_graphic() || b == b' ' {
                print!("{}", b as char);
            } else {
                print!(".");
            }
        }
        println!("|");
    }
}

/// Print one hexdump-style line: offset | raw bytes | name = value.
/// `name` is padded to 16 chars so all `=` signs stay in the same column.
/// Pass an empty `value` for fields with no decoded meaning (reserved bytes).
fn hexline(file_offset: usize, bytes: &[u8], name: &str, value: &str) {
    let hex = hex_abbrev(bytes, 4);
    if value.is_empty() {
        println!("  0x{file_offset:04x}  {hex:<17}  {name}");
    } else {
        println!("  0x{file_offset:04x}  {hex:<17}  {name:<16}= {value}");
    }
}

#[allow(clippy::too_many_lines, clippy::range_plus_one)]
fn cmd_show(path: &Path, show_payload: bool) -> Result<(), String> {
    let mut file = File::open(path).map_err(|e| format!("cannot open {}: {e}", path.display()))?;

    macro_rules! u16le {
        ($raw:expr, $pos:expr) => {
            u16::from_le_bytes($raw[$pos..$pos + 2].try_into().unwrap())
        };
    }
    macro_rules! u32le {
        ($raw:expr, $pos:expr) => {
            u32::from_le_bytes($raw[$pos..$pos + 4].try_into().unwrap())
        };
    }

    // ── CFHeader ────────────────────────────────────────────────────────────
    let (header, header_size) =
        CFHeader::read(&mut file, 0).map_err(|e| format!("failed to read CFHeader: {e}"))?;
    let raw = read_at(&mut file, 0, header_size)?;

    println!("=== CFHeader ({header_size} bytes @ 0x0000) ===");
    let mut pos = 0usize;
    hexline(
        pos,
        &raw[pos..pos + 4],
        "signature",
        &format!("0x{:08X} ('MSCF')", u32le!(raw, pos)),
    );
    pos += 4;
    hexline(pos, &raw[pos..pos + 4], "reserved1", "");
    pos += 4;
    hexline(
        pos,
        &raw[pos..pos + 4],
        "cbCabinet",
        &format!("{} bytes", u32le!(raw, pos)),
    );
    pos += 4;
    hexline(pos, &raw[pos..pos + 4], "reserved2", "");
    pos += 4;
    hexline(
        pos,
        &raw[pos..pos + 4],
        "coffFiles",
        &format!("0x{:04X}", u32le!(raw, pos)),
    );
    pos += 4;
    hexline(pos, &raw[pos..pos + 4], "reserved3", "");
    pos += 4;
    hexline(
        pos,
        &raw[pos..pos + 1],
        "versionMinor",
        &format!("{}", raw[pos]),
    );
    pos += 1;
    hexline(
        pos,
        &raw[pos..pos + 1],
        "versionMajor",
        &format!("{}", raw[pos]),
    );
    pos += 1;
    hexline(
        pos,
        &raw[pos..pos + 2],
        "cFolders",
        &format!("{}", u16le!(raw, pos)),
    );
    pos += 2;
    hexline(
        pos,
        &raw[pos..pos + 2],
        "cFiles",
        &format!("{}", u16le!(raw, pos)),
    );
    pos += 2;
    hexline(
        pos,
        &raw[pos..pos + 2],
        "flags",
        &format!("0x{:04X}", u16le!(raw, pos)),
    );
    pos += 2;
    hexline(
        pos,
        &raw[pos..pos + 2],
        "setID",
        &format!("{}", u16le!(raw, pos)),
    );
    pos += 2;
    hexline(
        pos,
        &raw[pos..pos + 2],
        "iCabinet",
        &format!("{}", u16le!(raw, pos)),
    );
    pos += 2;
    let _ = pos;

    let flags = CFHeaderFlags::from_bits(header.flags.get());
    let mut off = header_size;

    // ── CFHeaderReserve (optional) ───────────────────────────────────────────
    let (per_folder_reserved, per_data_reserved) = if flags.contains(CFHeaderFlag::HasReserve) {
        let (reserve, reserve_size) = CFHeaderReserve::read(&mut file, off)
            .map_err(|e| format!("failed to read CFHeaderReserve: {e}"))?;
        let raw = read_at(&mut file, off, reserve_size)?;
        println!("\n=== CFHeaderReserve ({reserve_size} bytes @ 0x{off:04x}) ===");
        let mut pos = 0usize;
        hexline(
            off + pos,
            &raw[pos..pos + 2],
            "cbCFHeader",
            &format!("{} bytes per-cabinet reserved", reserve.cbCFHeader.get()),
        );
        pos += 2;
        hexline(
            off + pos,
            &raw[pos..pos + 1],
            "cbCFFolder",
            &format!("{} bytes per-folder reserved", reserve.cbCFFolder),
        );
        pos += 1;
        hexline(
            off + pos,
            &raw[pos..pos + 1],
            "cbCFData",
            &format!("{} bytes per-data reserved", reserve.cbCFData),
        );
        pos += 1;
        let _ = pos;
        off += reserve_size + reserve.cbCFHeader.get() as usize;
        (reserve.cbCFFolder as usize, reserve.cbCFData as usize)
    } else {
        (0, 0)
    };

    // ── Prev/next cabinet strings (optional) ─────────────────────────────────
    if flags.contains(CFHeaderFlag::HasPrev) {
        let (prev_cab, n) = read_nul_string(&mut file, off)?;
        off += n;
        let (prev_disk, n) = read_nul_string(&mut file, off)?;
        off += n;
        println!("\n  prev_cabinet = {prev_cab:?}  prev_disk = {prev_disk:?}");
    }
    if flags.contains(CFHeaderFlag::HasNext) {
        let (next_cab, n) = read_nul_string(&mut file, off)?;
        off += n;
        let (next_disk, n) = read_nul_string(&mut file, off)?;
        off += n;
        println!("  next_cabinet = {next_cab:?}  next_disk = {next_disk:?}");
    }

    // ── CFFolder entries ──────────────────────────────────────────────────────
    let nfolders = header.cFolders.get() as usize;
    let folder_wire_size = CFFolder::size_in_bytes();
    let mut folder_data_offsets = Vec::with_capacity(nfolders);

    for i in 0..nfolders {
        let (folder, _) = CFFolder::read(&mut file, off)
            .map_err(|e| format!("failed to read CFFolder[{i}]: {e}"))?;
        let raw = read_at(&mut file, off, folder_wire_size + per_folder_reserved)?;
        println!(
            "\n=== CFFolder[{i}] ({} bytes @ 0x{off:04x}) ===",
            folder_wire_size + per_folder_reserved
        );
        let data_off = folder.coffCabStart.get() as usize;
        folder_data_offsets.push((data_off, folder.cCFData.get() as usize));
        let compress_name = match folder.typeCompress.get() & 0x000F {
            0 => "None",
            1 => "MsZip",
            _ => "Unknown",
        };
        let mut pos = 0usize;
        hexline(
            off + pos,
            &raw[pos..pos + 4],
            "coffCabStart",
            &format!("0x{data_off:04X} ({data_off})"),
        );
        pos += 4;
        hexline(
            off + pos,
            &raw[pos..pos + 2],
            "cCFData",
            &format!("{}", folder.cCFData.get()),
        );
        pos += 2;
        hexline(
            off + pos,
            &raw[pos..pos + 2],
            "typeCompress",
            &format!("0x{:04X} ({compress_name})", folder.typeCompress.get()),
        );
        pos += 2;
        if per_folder_reserved > 0 {
            hexline(
                off + pos,
                &raw[pos..pos + per_folder_reserved],
                &format!("[{per_folder_reserved} reserved bytes]"),
                "",
            );
        }
        off += folder_wire_size + per_folder_reserved;
    }

    // ── CFFile entries ────────────────────────────────────────────────────────
    let nfiles = header.cFiles.get() as usize;
    off = header.coffFiles.get() as usize;
    let file_wire_size = CFFile::size_in_bytes();

    for i in 0..nfiles {
        let (fhdr, _) =
            CFFile::read(&mut file, off).map_err(|e| format!("failed to read CFFile[{i}]: {e}"))?;
        let fixed = read_at(&mut file, off, file_wire_size)?;
        let (name, name_len) = read_nul_string(&mut file, off + file_wire_size)?;
        let name_bytes = read_at(&mut file, off + file_wire_size, name_len)?;
        println!(
            "\n=== CFFile[{i}] ({} bytes @ 0x{off:04x}) ===",
            file_wire_size + name_len
        );
        let mut pos = 0usize;
        hexline(
            off + pos,
            &fixed[pos..pos + 4],
            "cbFile",
            &format!("{} bytes", fhdr.cbFile.get()),
        );
        pos += 4;
        hexline(
            off + pos,
            &fixed[pos..pos + 4],
            "uoffFolderStart",
            &format!("{}", fhdr.uoffFolderStart.get()),
        );
        pos += 4;
        hexline(
            off + pos,
            &fixed[pos..pos + 2],
            "iFolder",
            &format!("{}", fhdr.iFolder.get()),
        );
        pos += 2;
        hexline(
            off + pos,
            &fixed[pos..pos + 2],
            "date",
            &format!("0x{:04X} ({})", u16le!(fixed, pos), fhdr.date),
        );
        pos += 2;
        hexline(
            off + pos,
            &fixed[pos..pos + 2],
            "time",
            &format!("0x{:04X} ({})", u16le!(fixed, pos), fhdr.time),
        );
        pos += 2;
        hexline(
            off + pos,
            &fixed[pos..pos + 2],
            "attribs",
            &format!("0x{:04X}", fhdr.attribs.get()),
        );
        pos += 2;
        let _ = pos;
        hexline(
            off + file_wire_size,
            &name_bytes,
            "name",
            &format!("{name:?}"),
        );
        off += file_wire_size + name_len;
    }

    // ── CFData blocks ─────────────────────────────────────────────────────────
    let data_hdr_size = CFData::size_in_bytes();
    for (fi, (mut data_off, nblocks)) in folder_data_offsets.into_iter().enumerate() {
        for bi in 0..nblocks {
            let (dh, _) = CFData::read(&mut file, data_off)
                .map_err(|e| format!("failed to read CFData[{fi}][{bi}]: {e}"))?;
            let sz = dh.cbData.get() as usize;
            let raw_hdr = read_at(&mut file, data_off, data_hdr_size)?;
            let payload = read_at(&mut file, data_off + data_hdr_size + per_data_reserved, sz)?;
            let csum_ok = if dh.csum.get() == 0 {
                "not verified (zero)".to_string()
            } else {
                let mut csum = Checksum::new();
                csum.update(&raw_hdr[4..8]); // cbData + cbUncomp
                csum.update(&payload);
                let computed = csum.checksum();
                if computed == dh.csum.get() {
                    "OK".to_string()
                } else {
                    format!("MISMATCH (computed 0x{computed:08X})")
                }
            };

            println!(
                "\n=== CFData[{fi}][{bi}] ({} + {sz} bytes @ 0x{data_off:04x}) ===",
                data_hdr_size + per_data_reserved
            );
            let mut pos = 0usize;
            hexline(
                data_off + pos,
                &raw_hdr[pos..pos + 4],
                "csum",
                &format!("0x{:08X}", dh.csum.get()),
            );
            pos += 4;
            hexline(
                data_off + pos,
                &raw_hdr[pos..pos + 2],
                "cbData",
                &format!("{} bytes compressed", dh.cbData.get()),
            );
            pos += 2;
            hexline(
                data_off + pos,
                &raw_hdr[pos..pos + 2],
                "cbUncomp",
                &format!("{} bytes", dh.cbUncomp.get()),
            );
            pos += 2;
            if per_data_reserved > 0 {
                let reserved = read_at(&mut file, data_off + pos, per_data_reserved)?;
                hexline(
                    data_off + pos,
                    &reserved,
                    &format!("[{per_data_reserved} reserved bytes]"),
                    "",
                );
            }
            hexline(
                data_off + data_hdr_size + per_data_reserved,
                &payload,
                "payload",
                &format!("{sz} bytes, checksum {csum_ok}"),
            );

            data_off += data_hdr_size + per_data_reserved + sz;
        }
    }

    if show_payload {
        let mut archive = open_archive(path, ParserFlags::empty())?;
        for entry in archive.files_mut() {
            let name = entry.name().to_string();
            println!("\n=== {name} ===");
            let mut data = Vec::new();
            entry
                .read_to_end(&mut data)
                .map_err(|e| format!("read error for {name}: {e}"))?;
            if is_text_extension(&name) {
                print!("{}", String::from_utf8_lossy(&data));
                if !data.ends_with(b"\n") {
                    println!();
                }
            } else {
                print_hexdump(&data);
            }
        }
    }

    Ok(())
}

fn main() {
    let cli = Cli::parse();

    let result = match &cli.command {
        Command::List {
            file,
            long,
            human_readable,
            ignore_checksum,
        } => {
            let mut flags = ParserFlags::empty();
            if *ignore_checksum {
                flags |= ParserFlag::IgnoreChecksum.as_mask();
            }
            cmd_list(file, flags, *long, *human_readable)
        }
        Command::Extract {
            file,
            output,
            ignore_checksum,
            only_basename,
        } => {
            let mut flags = ParserFlags::empty();
            if *ignore_checksum {
                flags |= ParserFlag::IgnoreChecksum.as_mask();
            }
            if *only_basename {
                flags |= ParserFlag::OnlyBasename.as_mask();
            }
            cmd_extract(file, output, flags)
        }
        Command::Show { file, show_payload } => cmd_show(file, *show_payload),
        Command::Create { output, files } => cmd_create(output, files),
    };

    if let Err(e) = result {
        eprintln!("error: {e}");
        std::process::exit(1);
    }
}
