// coldhex-flash: a small helper program for ColdHex.
//
// Why it exists: the `probe-rs` command line tool draws its progress bar for a human at a
// terminal and prints nothing a program could read. The probe-rs *library* reports exactly how
// many bytes were erased, written and verified. This helper uses the library and prints those
// events as one JSON object per line on stdout. ColdHex starts it with QProcess, reads the
// lines and shows a real percentage, like ST-Link Utility does.
//
// Usage (every option is `--name value`):
//   coldhex-flash erase    <target options> [--range 0x08000000:0x08100000] [--mass]
//   coldhex-flash download <file> <target options> --format hex|bin|elf
//                          [--base-address 0x08000000] [--verify]
//   coldhex-flash verify   <file> <target options> --format hex|bin|elf [--base-address 0x08000000]
//   coldhex-flash read     <target options> --address 0x08000000 --length 1048576 --output FILE
//   coldhex-flash info     [--probe VID:PID[:SERIAL]]   (only the probe: no chip is needed)
//
// Target options:
//   --chip NAME   --chip-description-path FILE.yaml   --protocol swd|jtag
//   --speed KHZ   --probe VID:PID[:SERIAL]            --connect-under-reset
//
// Output lines (stdout):
//   {"event":"plan","op":"erase","total":1048576}      what will be done, in bytes
//   {"event":"started","op":"erase"}
//   {"event":"progress","op":"erase","bytes":131072}   this many MORE bytes are done
//   {"event":"finished","op":"erase"}   or   {"event":"failed","op":"erase"}
//   {"event":"message","text":"..."}                    a note from the flash algorithm
//   {"event":"probe","name":"STLink V2","voltage":3.28}   answer of `info`; voltage is null when
//                                                        the probe cannot measure it
//   {"event":"result","ok":true}   or   {"event":"result","ok":false,"error":"..."}
// The exit code is 0 after ok:true and 1 after ok:false.
//
// A short guide for readers who know C++ but not Rust:
//   * `Result<T, String>` is "a T or an error text". `?` after a call means "if it failed,
//     return the error from this function now", like an early return after an error check.
//   * `match x { A => ..., B => ... }` is a switch that must cover every case.
//   * `|event| { ... }` is a lambda; `move` copies what it uses into the lambda.
//   * `&mut x` is a non-const reference, `&x` a const reference.

use probe_rs::config::{MemoryRegion, Registry};
use probe_rs::flashing::{
    self, BinOptions, DownloadOptions, ElfOptions, FlashProgress, ImageLoader, ProgressEvent,
    ProgressOperation,
};
use probe_rs::probe::list::Lister;
use probe_rs::probe::{DebugProbeSelector, WireProtocol};
use probe_rs::{MemoryInterface, Permissions, Session};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::io::Write;

/// Prints one JSON object as a line on stdout and flushes, so the reader sees it at once.
fn emit(value: Value) {
    let mut out = std::io::stdout().lock();
    // Writing to a closed pipe (ColdHex ended) is not worth a crash: ignore the result.
    let _ = writeln!(out, "{value}");
    let _ = out.flush();
}

/// The name used in the JSON lines for each kind of operation.
fn op_name(op: ProgressOperation) -> &'static str {
    match op {
        ProgressOperation::Erase => "erase",
        ProgressOperation::Program => "program",
        ProgressOperation::Verify => "verify",
        ProgressOperation::Fill => "fill",
    }
}

/// Turns a probe-rs progress event into a JSON line.
fn report(event: ProgressEvent) {
    match event {
        ProgressEvent::AddProgressBar { operation, total } => {
            emit(json!({"event": "plan", "op": op_name(operation), "total": total}));
        }
        ProgressEvent::Started(op) => emit(json!({"event": "started", "op": op_name(op)})),
        ProgressEvent::Progress { operation, size, .. } => {
            emit(json!({"event": "progress", "op": op_name(operation), "bytes": size}));
        }
        ProgressEvent::Finished(op) => emit(json!({"event": "finished", "op": op_name(op)})),
        ProgressEvent::Failed(op) => emit(json!({"event": "failed", "op": op_name(op)})),
        ProgressEvent::DiagnosticMessage { message } => {
            emit(json!({"event": "message", "text": message}));
        }
        // Not needed by ColdHex.
        ProgressEvent::FlashLayoutReady { .. } => {}
    }
}

/// The command line after the command name: `--name value` pairs, flags without a value, and
/// loose arguments (the file name).
struct Args {
    options: HashMap<String, String>,
    flags: Vec<String>,
    positional: Vec<String>,
}

/// Options that are switches and take no value.
const FLAGS: [&str; 4] = ["--mass", "--verify", "--connect-under-reset", "--chip-erase"];

fn parse_args(raw: &[String]) -> Args {
    let mut args = Args { options: HashMap::new(), flags: Vec::new(), positional: Vec::new() };
    let mut i = 0;
    while i < raw.len() {
        let item = &raw[i];
        if FLAGS.contains(&item.as_str()) {
            args.flags.push(item.clone());
        } else if item.starts_with("--") && i + 1 < raw.len() {
            args.options.insert(item.clone(), raw[i + 1].clone());
            i += 1; // the value was used up as well
        } else {
            args.positional.push(item.clone());
        }
        i += 1;
    }
    args
}

impl Args {
    fn flag(&self, name: &str) -> bool {
        self.flags.iter().any(|f| f == name)
    }
    fn option(&self, name: &str) -> Option<&String> {
        self.options.get(name)
    }
    fn required(&self, name: &str) -> Result<&String, String> {
        self.option(name).ok_or_else(|| format!("missing option {name}"))
    }
}

/// Opens the probe and attaches to the chip as the options ask.
fn connect(args: &Args) -> Result<Session, String> {
    // The chip database that is built into probe-rs, plus the user's own YAML file if given.
    let mut registry = Registry::from_builtin_families();
    if let Some(path) = args.option("--chip-description-path") {
        let yaml = std::fs::read_to_string(path)
            .map_err(|e| format!("cannot read chip description {path}: {e}"))?;
        registry
            .add_target_family_from_yaml(&yaml)
            .map_err(|e| format!("invalid chip description {path}: {e}"))?;
    }

    // Choose the probe: the one named by --probe, otherwise the first one found.
    let lister = Lister::new();
    let mut probe = match args.option("--probe") {
        Some(text) => {
            let selector: DebugProbeSelector =
                text.parse().map_err(|e| format!("bad --probe value {text}: {e}"))?;
            lister.open(selector).map_err(|e| format!("cannot open probe {text}: {e}"))?
        }
        None => {
            let all = lister.list_all();
            let first = all.first().ok_or("no debug probe found")?;
            first.open().map_err(|e| format!("cannot open the probe: {e}"))?
        }
    };

    let protocol = match args.option("--protocol").map(String::as_str) {
        Some("jtag") => WireProtocol::Jtag,
        _ => WireProtocol::Swd,
    };
    probe.select_protocol(protocol).map_err(|e| format!("cannot select the protocol: {e}"))?;

    if let Some(speed) = args.option("--speed") {
        let khz: u32 = speed.parse().map_err(|_| format!("bad --speed value {speed}"))?;
        // The probe may use a different speed than asked; that is fine, probe-rs tells us.
        probe.set_speed(khz).map_err(|e| format!("cannot set the speed: {e}"))?;
    }

    let chip = args.required("--chip")?;
    let permissions = Permissions::default();
    let session = if args.flag("--connect-under-reset") {
        probe.attach_under_reset_with_registry(chip.as_str(), permissions, &registry)
    } else {
        probe.attach_with_registry(chip.as_str(), permissions, &registry)
    };
    session.map_err(|e| format!("cannot connect to {chip}: {e}"))
}

/// `info`: opens the probe without attaching to a chip and reports its name and the voltage it
/// measures on the target's VTref pin. Probes that cannot measure it (CMSIS-DAP) report null.
fn run_info(args: &Args) -> Result<(), String> {
    let lister = Lister::new();
    let mut probe = match args.option("--probe") {
        Some(text) => {
            let selector: DebugProbeSelector =
                text.parse().map_err(|e| format!("bad --probe value {text}: {e}"))?;
            lister.open(selector).map_err(|e| format!("cannot open probe {text}: {e}"))?
        }
        None => {
            let all = lister.list_all();
            let first = all.first().ok_or("no debug probe found")?;
            first.open().map_err(|e| format!("cannot open the probe: {e}"))?
        }
    };
    let name = probe.get_name();
    // An error here means "cannot tell", the same as Ok(None): both are reported as null.
    let voltage = probe.get_target_voltage().ok().flatten();
    emit(json!({"event": "probe", "name": name, "voltage": voltage}));
    Ok(())
}

/// Reads an address like 0x1000 (hexadecimal, with or without the 0x).
fn parse_address(text: &str) -> Result<u64, String> {
    let digits = text.trim_start_matches("0x").trim_start_matches("0X");
    u64::from_str_radix(digits, 16).map_err(|_| format!("bad address {text}"))
}

/// Reads "START:END" (both hexadecimal) into a pair of addresses. END is the first address
/// after the area, so 0x08000000:0x08100000 is 1 MiB of flash.
fn parse_range(text: &str) -> Result<(u64, u64), String> {
    let (start, end) = text.split_once(':').ok_or_else(|| format!("bad --range {text}"))?;
    let (start, end) = (parse_address(start)?, parse_address(end)?);
    if end <= start {
        return Err(format!("bad --range {text}: the end must be after the start"));
    }
    Ok((start, end))
}

/// `erase`: by default sector by sector, because then probe-rs reports every finished sector
/// and ColdHex can show a real percentage. `--mass` uses the chip's own mass erase instead,
/// which is a single step without any progress.
fn run_erase(args: &Args) -> Result<(), String> {
    let mut session = connect(args)?;
    let mut progress = FlashProgress::new(report);

    if args.flag("--mass") {
        emit(json!({"event": "plan", "op": "erase", "total": Value::Null}));
        return flashing::erase_all(&mut session, &mut progress, false)
            .map_err(|e| format!("erase failed: {e}"));
    }

    // What to erase: the range ColdHex gave with --range START:END, otherwise all real flash
    // areas of the chip (copies of the same memory are skipped). ColdHex passes the main flash
    // so that small special areas such as the one-time-programmable bytes are left alone.
    let ranges: Vec<(u64, u64)> = match args.option("--range") {
        Some(text) => vec![parse_range(text)?],
        None => session
            .target()
            .memory_map
            .iter()
            .filter_map(MemoryRegion::as_nvm_region)
            .filter(|region| !region.is_alias)
            .map(|region| (region.range.start, region.range.end))
            .collect(),
    };
    if ranges.is_empty() {
        return Err("this chip has no flash memory to erase".to_string());
    }

    // The library does not announce the total for a range erase, so we do it.
    let total: u64 = ranges.iter().map(|(start, end)| end - start).sum();
    emit(json!({"event": "plan", "op": "erase", "total": total}));

    for (start, end) in ranges {
        flashing::erase(&mut session, &mut progress, start, end, false)
            .map_err(|e| format!("erase failed: {e}"))?;
    }
    Ok(())
}

/// The firmware file's format as a value probe-rs understands. For `bin` the address where the
/// file belongs comes from `--base-address`.
fn image_loader(args: &Args) -> Result<Box<dyn ImageLoader>, String> {
    match args.required("--format")?.as_str() {
        "hex" => Ok(Box::new(flashing::HexLoader)),
        "elf" => Ok(Box::new(flashing::ElfLoader(ElfOptions::default()))),
        "bin" => {
            let mut bin = BinOptions::default();
            if let Some(text) = args.option("--base-address") {
                bin.base_address = Some(parse_address(text)?);
            }
            Ok(Box::new(flashing::BinLoader(bin)))
        }
        other => Err(format!("unknown --format {other} (use hex, bin or elf)")),
    }
}

/// `download`: writes a firmware file. probe-rs erases only the sectors it needs, writes the
/// pages and, with `--verify`, reads everything back; each of these reports its own progress.
fn run_download(args: &Args) -> Result<(), String> {
    let path = args.positional.first().ok_or("missing firmware file")?;
    let loader = image_loader(args)?;
    let mut session = connect(args)?;

    let mut options = DownloadOptions::default();
    options.progress = FlashProgress::new(report);
    options.verify = args.flag("--verify");
    options.do_chip_erase = args.flag("--chip-erase");

    flashing::download_file_with_options(&mut session, path, loader, options)
        .map_err(|e| format!("download failed: {e}"))
}

/// `verify`: compares the chip with a firmware file and writes nothing. A difference is an
/// error ("the chip does not match the file").
fn run_verify(args: &Args) -> Result<(), String> {
    let path = args.positional.first().ok_or("missing firmware file")?;
    let loader = image_loader(args)?;
    let mut session = connect(args)?;

    let flash_loader = flashing::build_loader(&mut session, path, loader, None)
        .map_err(|e| format!("cannot read {path}: {e}"))?;
    let mut progress = FlashProgress::new(report);
    flash_loader
        .verify(&mut session, &mut progress)
        .map_err(|e| format!("verification failed: {e}"))
}

/// How many bytes `read` fetches per request. Each request is one progress step.
const READ_CHUNK: u64 = 64 * 1024;

/// `read`: copies a memory area of the chip into a file. The library reports no progress for a
/// plain memory read, so the area is read in chunks and every chunk is reported here.
fn run_read(args: &Args) -> Result<(), String> {
    let address = parse_address(args.required("--address")?)?;
    let text = args.required("--length")?;
    let length: u64 = text.parse().map_err(|_| format!("bad --length value {text}"))?;
    let output = args.required("--output")?;
    if length == 0 || length % 4 != 0 || address % 4 != 0 {
        return Err("--address and --length must be multiples of 4 and the length not zero".to_string());
    }

    let mut session = connect(args)?;
    let mut core = session.core(0).map_err(|e| format!("cannot access the core: {e}"))?;
    let mut file = std::fs::File::create(output).map_err(|e| format!("cannot create {output}: {e}"))?;

    emit(json!({"event": "plan", "op": "read", "total": length}));
    emit(json!({"event": "started", "op": "read"}));

    let mut words = vec![0u32; (READ_CHUNK / 4) as usize];
    let mut done = 0;
    while done < length {
        let bytes = READ_CHUNK.min(length - done);
        let chunk = &mut words[..(bytes / 4) as usize];
        // 32-bit reads are much faster over the debug link than byte reads.
        core.read_32(address + done, chunk).map_err(|e| format!("read failed at 0x{:08x}: {e}", address + done))?;
        for word in chunk.iter() {
            file.write_all(&word.to_le_bytes()).map_err(|e| format!("cannot write {output}: {e}"))?;
        }
        done += bytes;
        emit(json!({"event": "progress", "op": "read", "bytes": bytes}));
    }
    emit(json!({"event": "finished", "op": "read"}));
    Ok(())
}

fn main() {
    let raw: Vec<String> = std::env::args().skip(1).collect(); // skip the program name
    let Some(command) = raw.first().cloned() else {
        emit(json!({"event": "result", "ok": false, "error": "no command (use erase, download, verify, read or info)"}));
        std::process::exit(1);
    };
    let args = parse_args(&raw[1..]);

    let outcome = match command.as_str() {
        "erase" => run_erase(&args),
        "download" => run_download(&args),
        "verify" => run_verify(&args),
        "read" => run_read(&args),
        "info" => run_info(&args),
        other => Err(format!("unknown command {other}")),
    };

    match outcome {
        Ok(()) => emit(json!({"event": "result", "ok": true})),
        Err(error) => {
            emit(json!({"event": "result", "ok": false, "error": error}));
            std::process::exit(1);
        }
    }
}
