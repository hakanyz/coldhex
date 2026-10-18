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
//   coldhex-flash capabilities --chip NAME   (what Lock/Unlock/status can do for the chip; no probe)
//   coldhex-flash protection <target options>   (reports the read-out protection level)
//   coldhex-flash lock     <target options>   (sets read-out protection level 1)
//   coldhex-flash unlock   <target options>   (removes read-out protection level 1; the chip
//                          erases its whole flash)
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
#[derive(Clone)]
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
    connect_with(args, Permissions::default())
}

/// Same as `connect`, with the permissions the command needs (for example erasing all).
fn connect_with(args: &Args, permissions: Permissions) -> Result<Session, String> {
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

/// How the read-out protection of a chip is removed.
enum Unlock {
    /// Flash controller of STM32F0/F1/F3 and its clones: erase the option bytes, then write
    /// the "unprotected" key (0xA5 on F1 and its clones, 0xAA on F0/F3 and their clones).
    OptionByteErase(u8),
    /// Artery AT32: erase the user system data and write the FAP key 0xA5 at `usd`; the FLASH
    /// registers are at `flash`.
    UsdErase { flash: u64, usd: u64, halfwords: usize },
    /// Flash controller of STM32F2/F4/F7 and its clones: write RDP = 0xAA into FLASH_OPTCR.
    OptionControl,
    /// STM32L4, G0, G4, C0, WB, WL: the RDP byte is the low byte of FLASH_OPTR at `base` + 0x20.
    OptionRegister(u64),
    /// STM32L5: like OptionRegister, but the registers lie differently and TrustZone (TZEN) is
    /// switched off together with the protection.
    OptionRegisterNs(u64),
    /// STM32H7: RDP is bits 15:8 of FLASH_OPTSR_PRG at `base`; FLASH_OPTCR.OPTSTART applies it.
    OptionStart(u64),
    /// STM32L0, L1: the RDP key is a word in the option bytes at 0x1FF80000, written after
    /// unlocking the data EEPROM controller at `base`.
    Eeprom(u64),
    /// The chip's own mass erase through the debug port, which probe-rs knows (nRF, SAM D/E/L,
    /// EFM32/EFR32).
    MassErase,
}

/// Picks the way to unlock by the chip name. None = no recipe for this chip yet.
fn unlock_method(chip: &str) -> Option<Unlock> {
    let name = chip.to_ascii_uppercase();
    let starts = |prefixes: &[&str]| prefixes.iter().any(|p| name.starts_with(p));
    if starts(&["STM32F0", "STM32F3", "APM32F0", "HK32F0"]) {
        Some(Unlock::OptionByteErase(0xAA))
    } else if starts(&["STM32F1", "GD32F1", "GD32F3", "GD32E1", "GD32E2", "APM32F1", "AIR32F1", "CH32F1"]) {
        Some(Unlock::OptionByteErase(0xA5))
    } else if starts(&["AT32F403", "AT32F407", "AT32A403", "AT32F413"]) {
        Some(Unlock::UsdErase { flash: 0x4002_2000, usd: 0x1FFF_F800, halfwords: 24 })
    } else if starts(&["AT32F415", "AT32F421", "AT32F425"]) {
        Some(Unlock::UsdErase { flash: 0x4002_2000, usd: 0x1FFF_F800, halfwords: 8 })
    } else if starts(&["AT32F423", "AT32F402", "AT32F405"]) {
        Some(Unlock::UsdErase { flash: 0x4002_3C00, usd: 0x1FFF_F800, halfwords: 8 })
    } else if starts(&["AT32F435", "AT32F437"]) {
        Some(Unlock::UsdErase { flash: 0x4002_3C00, usd: 0x1FFF_C000, halfwords: 34 })
    } else if starts(&["PY32F002A", "PY32F003", "PY32F030", "PY32F031"]) {
        // Same flash controller layout as the STM32L4 family (FLASH_OPTR, RDP 0xAA = none).
        Some(Unlock::OptionRegister(0x4002_2000))
    } else if starts(&["STM32F2", "STM32F4", "STM32F7", "GD32F4", "APM32F4"]) {
        Some(Unlock::OptionControl)
    } else if starts(&["STM32L4", "STM32G0", "STM32G4", "STM32C0"]) {
        Some(Unlock::OptionRegister(0x4002_2000))
    } else if starts(&["STM32L5"]) {
        Some(Unlock::OptionRegisterNs(0x4002_2000))
    } else if starts(&["STM32H7"]) && !starts(&["STM32H7R", "STM32H7S"]) {
        Some(Unlock::OptionStart(0x5200_2000))
    } else if starts(&["STM32WB5", "STM32WB3", "STM32WB1", "STM32WL"]) {
        Some(Unlock::OptionRegister(0x5800_4000))
    } else if starts(&["STM32L0"]) {
        Some(Unlock::Eeprom(0x4002_2000))
    } else if starts(&["STM32L1"]) {
        Some(Unlock::Eeprom(0x4002_3C00))
    } else if starts(&["NRF", "SAMD", "SAME", "SAML", "EFM32", "EFR32"]) {
        Some(Unlock::MassErase)
    } else {
        None
    }
}

const FLASH_KEY1: u32 = 0x4567_0123;
const FLASH_KEY2: u32 = 0xCDEF_89AB;
const OPT_KEY1: u32 = 0x0819_2A3B;
const OPT_KEY2: u32 = 0x4C5D_6E7F;

/// Waits until the bits in `mask` of the register at `address` are zero (the flash controller
/// is done). Erasing a protected chip removes the whole flash, which takes a while.
fn wait_not_busy(core: &mut probe_rs::Core, address: u64, mask: u32) -> Result<(), String> {
    let start = std::time::Instant::now();
    loop {
        let value = core.read_word_32(address).map_err(|e| format!("cannot read the flash status: {e}"))?;
        if value & mask == 0 {
            return Ok(());
        }
        if start.elapsed() > std::time::Duration::from_secs(60) {
            return Err("the flash controller stayed busy for 60 seconds".to_string());
        }
        std::thread::sleep(std::time::Duration::from_millis(5));
    }
}

/// STM32F0/F1/F3 style: FLASH registers at 0x40022000, option bytes at 0x1FFFF800.
fn unlock_option_byte_erase(core: &mut probe_rs::Core, key: u8) -> Result<(), String> {
    const BASE: u64 = 0x4002_2000;
    const KEYR: u64 = BASE + 0x04;
    const OPTKEYR: u64 = BASE + 0x08;
    const SR: u64 = BASE + 0x0C;
    const CR: u64 = BASE + 0x10;
    const BSY: u32 = 1 << 0;
    const OPTPG: u32 = 1 << 4;
    const OPTER: u32 = 1 << 5;
    const STRT: u32 = 1 << 6;
    const OPTWRE: u32 = 1 << 9;
    const OPTION_BYTES: u64 = 0x1FFF_F800;
    let e = |what: &str, err: probe_rs::Error| format!("{what}: {err}");

    core.write_word_32(KEYR, FLASH_KEY1).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(KEYR, FLASH_KEY2).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(OPTKEYR, FLASH_KEY1).map_err(|x| e("option unlock", x))?;
    core.write_word_32(OPTKEYR, FLASH_KEY2).map_err(|x| e("option unlock", x))?;
    let cr = core.read_word_32(CR).map_err(|x| e("read FLASH_CR", x))?;
    if cr & OPTWRE == 0 {
        return Err("the option bytes could not be unlocked (FLASH_CR.OPTWRE stays 0)".to_string());
    }

    emit(json!({"event": "message", "text": "Erasing the option bytes; the chip erases its whole flash..."}));
    wait_not_busy(core, SR, BSY)?;
    core.write_word_32(CR, cr | OPTER).map_err(|x| e("write FLASH_CR", x))?;
    core.write_word_32(CR, cr | OPTER | STRT).map_err(|x| e("write FLASH_CR", x))?;
    wait_not_busy(core, SR, BSY)?;
    core.write_word_32(CR, cr & !OPTER).map_err(|x| e("write FLASH_CR", x))?;

    // The erased option bytes would leave the chip protected (RDP = 0xFF): write the key
    // that means "no protection". The chip adds the complement byte itself.
    emit(json!({"event": "message", "text": format!("Writing the RDP key 0x{key:02X}...")}));
    core.write_word_32(CR, cr | OPTPG).map_err(|x| e("write FLASH_CR", x))?;
    core.write_word_16(OPTION_BYTES, u16::from(key) | (u16::from(!key) << 8)).map_err(|x| e("write option bytes", x))?;
    wait_not_busy(core, SR, BSY)?;
    core.write_word_32(CR, cr & !OPTPG).map_err(|x| e("write FLASH_CR", x))?;
    Ok(())
}

/// Artery AT32 (all series share this layout; only the two addresses differ): unlock the user
/// system data, erase it, then program the FAP key 0xA5 ("no protection"). Erasing a protected
/// chip erases its flash. Returns false when the chip is not protected.
fn unlock_usd_erase(core: &mut probe_rs::Core, flash: u64, usd: u64) -> Result<bool, String> {
    let keyr = flash + 0x04;
    let usd_unlock = flash + 0x08;
    let sts = flash + 0x0C;
    let ctrl = flash + 0x10;
    let usd_reg = flash + 0x1C;
    const OBF: u32 = 1 << 0;
    const USDPRGM: u32 = 1 << 4;
    const USDERS: u32 = 1 << 5;
    const ERSTR: u32 = 1 << 6;
    const USDULKS: u32 = 1 << 9;
    const FAP: u32 = 1 << 1;
    let e = |what: &str, err: probe_rs::Error| format!("{what}: {err}");

    let status = core.read_word_32(usd_reg).map_err(|x| e("read FLASH_USD", x))?;
    if status & FAP == 0 {
        return Ok(false);
    }

    core.write_word_32(keyr, FLASH_KEY1).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(keyr, FLASH_KEY2).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(usd_unlock, FLASH_KEY1).map_err(|x| e("user data unlock", x))?;
    core.write_word_32(usd_unlock, FLASH_KEY2).map_err(|x| e("user data unlock", x))?;
    let control = core.read_word_32(ctrl).map_err(|x| e("read FLASH_CTRL", x))?;
    if control & USDULKS == 0 {
        return Err("the user system data could not be unlocked (FLASH_CTRL.USDULKS stays 0)".to_string());
    }

    emit(json!({"event": "message", "text": "Erasing the user system data; the chip erases its whole flash..."}));
    wait_not_busy(core, sts, OBF)?;
    core.write_word_32(ctrl, control | USDERS).map_err(|x| e("write FLASH_CTRL", x))?;
    core.write_word_32(ctrl, control | USDERS | ERSTR).map_err(|x| e("write FLASH_CTRL", x))?;
    wait_not_busy(core, sts, OBF)?;
    core.write_word_32(ctrl, control & !USDERS).map_err(|x| e("write FLASH_CTRL", x))?;

    emit(json!({"event": "message", "text": "Writing the FAP key 0xA5..."}));
    core.write_word_32(ctrl, control | USDPRGM).map_err(|x| e("write FLASH_CTRL", x))?;
    core.write_word_16(usd, 0x00A5).map_err(|x| e("write the user system data", x))?;
    wait_not_busy(core, sts, OBF)?;
    core.write_word_32(ctrl, control & !USDPRGM).map_err(|x| e("write FLASH_CTRL", x))?;
    Ok(true)
}

/// STM32F2/F4/F7 style: FLASH registers at 0x40023C00, RDP byte in FLASH_OPTCR bits 15:8.
fn unlock_option_control(core: &mut probe_rs::Core) -> Result<(), String> {
    const BASE: u64 = 0x4002_3C00;
    const KEYR: u64 = BASE + 0x04;
    const OPTKEYR: u64 = BASE + 0x08;
    const SR: u64 = BASE + 0x0C;
    const OPTCR: u64 = BASE + 0x14;
    const BSY: u32 = 1 << 16;
    const OPTLOCK: u32 = 1 << 0;
    const OPTSTRT: u32 = 1 << 1;
    const RDP_MASK: u32 = 0xFF << 8;
    const RDP_NONE: u32 = 0xAA << 8;
    let e = |what: &str, err: probe_rs::Error| format!("{what}: {err}");

    let before = core.read_word_32(OPTCR).map_err(|x| e("read FLASH_OPTCR", x))?;
    if before & RDP_MASK == RDP_NONE {
        emit(json!({"event": "message", "text": "The chip is not read-protected (RDP is already 0xAA)."}));
        return Ok(());
    }
    if before & RDP_MASK == (0xCC << 8) {
        return Err("RDP level 2 is permanent: this chip cannot be unlocked".to_string());
    }

    core.write_word_32(KEYR, FLASH_KEY1).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(KEYR, FLASH_KEY2).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(OPTKEYR, OPT_KEY1).map_err(|x| e("option unlock", x))?;
    core.write_word_32(OPTKEYR, OPT_KEY2).map_err(|x| e("option unlock", x))?;
    let optcr = core.read_word_32(OPTCR).map_err(|x| e("read FLASH_OPTCR", x))?;
    if optcr & OPTLOCK != 0 {
        return Err("the option bytes could not be unlocked (FLASH_OPTCR.OPTLOCK stays 1)".to_string());
    }

    emit(json!({"event": "message", "text": "Setting RDP to level 0; the chip erases its whole flash..."}));
    wait_not_busy(core, SR, BSY)?;
    core.write_word_32(OPTCR, (optcr & !RDP_MASK) | RDP_NONE | OPTSTRT)
        .map_err(|x| e("write FLASH_OPTCR", x))?;
    wait_not_busy(core, SR, BSY)?;
    Ok(())
}

/// STM32L4/G0/G4/C0/WB/WL: sets the RDP byte of FLASH_OPTR. 0xAA removes the protection (the
/// chip then erases its flash), any other value except 0xCC sets level 1. Returns false when
/// the byte already has the wanted value.
fn set_rdp_register(core: &mut probe_rs::Core, base: u64, rdp: u32) -> Result<bool, String> {
    set_rdp_optr(core, base, RegLayout::L4, rdp)
}

/// Where the flash registers lie, counted from the flash controller base.
#[derive(Clone, Copy)]
struct RegLayout { keyr: u64, optkeyr: u64, sr: u64, cr: u64, optr: u64 }

impl RegLayout {
    /// STM32L4, G0, G4, C0, WB, WL, PY32F0.
    const L4: RegLayout = RegLayout { keyr: 0x08, optkeyr: 0x0C, sr: 0x10, cr: 0x14, optr: 0x20 };
    /// STM32L5 (the non-secure registers; option bytes are always changed through these).
    const L5: RegLayout = RegLayout { keyr: 0x08, optkeyr: 0x10, sr: 0x20, cr: 0x28, optr: 0x40 };
}

/// Changes the RDP byte of FLASH_OPTR. On a TrustZone chip (OPTR bit 31, TZEN) removing the
/// protection has to clear TZEN in the same write; that is what ST's own tools do (regression).
fn set_rdp_optr(core: &mut probe_rs::Core, base: u64, layout: RegLayout, rdp: u32) -> Result<bool, String> {
    let keyr = base + layout.keyr;
    let optkeyr = base + layout.optkeyr;
    let sr = base + layout.sr;
    let cr = base + layout.cr;
    let optr = base + layout.optr;
    const BSY: u32 = 1 << 16;
    const OPTSTRT: u32 = 1 << 17;
    const OPTLOCK: u32 = 1 << 30;
    const LOCK: u32 = 1 << 31;
    let e = |what: &str, err: probe_rs::Error| format!("{what}: {err}");
    debug_assert!(rdp != 0xCC, "level 2 must never be written");

    let before = core.read_word_32(optr).map_err(|x| e("read FLASH_OPTR", x))?;
    let wanted_level0 = rdp == 0xAA;
    if before & 0xFF == 0xCC {
        return Err("RDP level 2 is permanent: this chip cannot be changed".to_string());
    }
    if (before & 0xFF == 0xAA) == wanted_level0 && !(wanted_level0 && layout.optr == RegLayout::L5.optr && before >> 31 != 0) {
        return Ok(false); // already in the wanted state
    }
    if !wanted_level0 && layout.optr == RegLayout::L5.optr && before >> 31 != 0 {
        return Err("locking a chip with TrustZone enabled is not supported yet".to_string());
    }

    core.write_word_32(keyr, FLASH_KEY1).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(keyr, FLASH_KEY2).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(optkeyr, OPT_KEY1).map_err(|x| e("option unlock", x))?;
    core.write_word_32(optkeyr, OPT_KEY2).map_err(|x| e("option unlock", x))?;
    let control = core.read_word_32(cr).map_err(|x| e("read FLASH_CR", x))?;
    if control & OPTLOCK != 0 {
        return Err("the option bytes could not be unlocked (FLASH_CR.OPTLOCK stays 1)".to_string());
    }

    wait_not_busy(core, sr, BSY)?;
    const TZEN: u32 = 1 << 31;
    let mut value = (before & !0xFF) | rdp;
    if wanted_level0 && layout.optr == RegLayout::L5.optr {
        value &= !TZEN;
    }
    core.write_word_32(optr, value).map_err(|x| e("write FLASH_OPTR", x))?;
    core.write_word_32(cr, control | OPTSTRT).map_err(|x| e("write FLASH_CR", x))?;
    wait_not_busy(core, sr, BSY)?;
    core.write_word_32(cr, LOCK | OPTLOCK).map_err(|x| e("lock the flash", x))?;
    Ok(true)
}

/// STM32H7 (not H7RS): changes the RDP byte through FLASH_OPTSR_PRG. Returns false when the
/// chip is already in the wanted state.
fn set_rdp_h7(core: &mut probe_rs::Core, base: u64, rdp: u32) -> Result<bool, String> {
    let optkeyr = base + 0x08;
    let optcr = base + 0x18;
    let optsr_cur = base + 0x1C;
    let optsr_prg = base + 0x20;
    let optccr = base + 0x24;
    const OPT_LOCK: u32 = 1 << 0;
    const OPT_START: u32 = 1 << 1;
    const OPT_BSY: u32 = 1 << 0;
    const OPT_CHANGE_ERR: u32 = 1 << 30;
    let e = |what: &str, err: probe_rs::Error| format!("{what}: {err}");
    debug_assert!(rdp != 0xCC, "level 2 must never be written");

    let current = core.read_word_32(optsr_cur).map_err(|x| e("read FLASH_OPTSR_CUR", x))?;
    let level = (current >> 8) & 0xFF;
    if level == 0xCC {
        return Err("RDP level 2 is permanent: this chip cannot be changed".to_string());
    }
    if (level == 0xAA) == (rdp == 0xAA) {
        return Ok(false);
    }

    let control = core.read_word_32(optcr).map_err(|x| e("read FLASH_OPTCR", x))?;
    if control & OPT_LOCK != 0 {
        core.write_word_32(optkeyr, OPT_KEY1).map_err(|x| e("option unlock", x))?;
        core.write_word_32(optkeyr, OPT_KEY2).map_err(|x| e("option unlock", x))?;
        let control = core.read_word_32(optcr).map_err(|x| e("read FLASH_OPTCR", x))?;
        if control & OPT_LOCK != 0 {
            return Err("the option bytes could not be unlocked (FLASH_OPTCR.OPTLOCK stays 1)".to_string());
        }
    }

    let programmed = core.read_word_32(optsr_prg).map_err(|x| e("read FLASH_OPTSR_PRG", x))?;
    core.write_word_32(optsr_prg, (programmed & !(0xFF << 8)) | (rdp << 8))
        .map_err(|x| e("write FLASH_OPTSR_PRG", x))?;
    core.write_word_32(optccr, OPT_CHANGE_ERR).map_err(|x| e("clear the option error", x))?;
    core.write_word_32(optcr, OPT_START).map_err(|x| e("start the option change", x))?;
    wait_not_busy(core, optsr_cur, OPT_BSY)?;
    let status = core.read_word_32(optsr_cur).map_err(|x| e("read FLASH_OPTSR_CUR", x))?;
    core.write_word_32(optcr, OPT_LOCK).map_err(|x| e("lock the options", x))?;
    if status & OPT_CHANGE_ERR != 0 {
        return Err("the chip refused the option change (OPTCHANGEERR)".to_string());
    }
    Ok(true)
}

/// STM32L0/L1: the RDP key is the first word of the option bytes. 0xFF5500AA = level 0,
/// 0xFFFF0000 = level 1 (the values OpenOCD uses). Returns false when it is already so.
fn set_rdp_eeprom(core: &mut probe_rs::Core, base: u64, unlock: bool) -> Result<bool, String> {
    const PEKEY1: u32 = 0x89AB_CDEF;
    const PEKEY2: u32 = 0x0203_0405;
    const OPTKEY1: u32 = 0xFBEA_D9C8;
    const OPTKEY2: u32 = 0x2425_2627;
    const OPTION_BYTES: u64 = 0x1FF8_0000;
    let pecr = base + 0x04;
    let pekeyr = base + 0x0C;
    let optkeyr = base + 0x14;
    let sr = base + 0x18;
    let obr = base + 0x1C;
    const PELOCK: u32 = 1 << 0;
    const OPTLOCK: u32 = 1 << 2;
    const BSY: u32 = 1 << 0;
    let e = |what: &str, err: probe_rs::Error| format!("{what}: {err}");

    let rdp = core.read_word_32(obr).map_err(|x| e("read FLASH_OBR", x))? & 0xFF;
    if rdp == 0xCC {
        return Err("RDP level 2 is permanent: this chip cannot be changed".to_string());
    }
    if (rdp == 0xAA) == unlock {
        return Ok(false);
    }

    let control = core.read_word_32(pecr).map_err(|x| e("read FLASH_PECR", x))?;
    if control & PELOCK != 0 {
        core.write_word_32(pekeyr, PEKEY1).map_err(|x| e("PECR unlock", x))?;
        core.write_word_32(pekeyr, PEKEY2).map_err(|x| e("PECR unlock", x))?;
    }
    core.write_word_32(optkeyr, OPTKEY1).map_err(|x| e("option unlock", x))?;
    core.write_word_32(optkeyr, OPTKEY2).map_err(|x| e("option unlock", x))?;
    let control = core.read_word_32(pecr).map_err(|x| e("read FLASH_PECR", x))?;
    if control & OPTLOCK != 0 {
        return Err("the option bytes could not be unlocked (FLASH_PECR.OPTLOCK stays 1)".to_string());
    }

    let key = if unlock { 0xFF55_00AA } else { 0xFFFF_0000 };
    core.write_word_32(OPTION_BYTES, key).map_err(|x| e("write the option bytes", x))?;
    wait_not_busy(core, sr, BSY)?;
    Ok(true)
}

/// The RDP value `lock` writes: any value other than 0xAA (none) and 0xCC (level 2, permanent)
/// means level 1. It is a fixed constant on purpose: level 2 can never be written by mistake.
const RDP_LEVEL1: u32 = 0x55;

/// STM32F2/F4/F7 style lock: RDP = level 1. Level 1 does not erase the flash and can be
/// removed again with `unlock` (which then erases it).
fn lock_option_control(core: &mut probe_rs::Core) -> Result<bool, String> {
    const BASE: u64 = 0x4002_3C00;
    const KEYR: u64 = BASE + 0x04;
    const OPTKEYR: u64 = BASE + 0x08;
    const SR: u64 = BASE + 0x0C;
    const OPTCR: u64 = BASE + 0x14;
    const BSY: u32 = 1 << 16;
    const OPTLOCK: u32 = 1 << 0;
    const OPTSTRT: u32 = 1 << 1;
    const RDP_MASK: u32 = 0xFF << 8;
    let e = |what: &str, err: probe_rs::Error| format!("{what}: {err}");

    let before = core.read_word_32(OPTCR).map_err(|x| e("read FLASH_OPTCR", x))?;
    if before & RDP_MASK != 0xAA << 8 {
        return Ok(false); // already protected: nothing was written
    }

    core.write_word_32(KEYR, FLASH_KEY1).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(KEYR, FLASH_KEY2).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(OPTKEYR, OPT_KEY1).map_err(|x| e("option unlock", x))?;
    core.write_word_32(OPTKEYR, OPT_KEY2).map_err(|x| e("option unlock", x))?;
    let optcr = core.read_word_32(OPTCR).map_err(|x| e("read FLASH_OPTCR", x))?;
    if optcr & OPTLOCK != 0 {
        return Err("the option bytes could not be unlocked (FLASH_OPTCR.OPTLOCK stays 1)".to_string());
    }

    wait_not_busy(core, SR, BSY)?;
    core.write_word_32(OPTCR, (optcr & !RDP_MASK) | (RDP_LEVEL1 << 8) | OPTSTRT)
        .map_err(|x| e("write FLASH_OPTCR", x))?;
    wait_not_busy(core, SR, BSY)?;
    Ok(true)
}

/// The read-out protection level of the chip (0 = none, 1, 2), or None when this chip family
/// has no known way to read it. Only registers that are certain are used.
fn read_protection(core: &mut probe_rs::Core, chip: &str) -> Result<Option<u8>, String> {
    let name = chip.to_ascii_uppercase();
    let starts = |prefixes: &[&str]| prefixes.iter().any(|p| name.starts_with(p));
    let e = |err: probe_rs::Error| format!("cannot read the protection register: {err}");

    match unlock_method(chip) {
        // FLASH_OPTCR bits 15:8: 0xAA = level 0, 0xCC = level 2, anything else = level 1.
        Some(Unlock::OptionControl) => {
            let optcr = core.read_word_32(0x4002_3C14).map_err(e)?;
            Ok(Some(match (optcr >> 8) & 0xFF {
                0xAA => 0,
                0xCC => 2,
                _ => 1,
            }))
        }
        // FLASH_OBR bit 1 (RDPRT) is set while the flash is protected. On F0/F3 bit 2 says level 2;
        // on F1 that bit is a user option, so F1 and its clones report level 1 at most.
        Some(Unlock::OptionByteErase(_)) => {
            let obr = core.read_word_32(0x4002_201C).map_err(e)?;
            Ok(Some(if obr & (1 << 1) == 0 {
                0
            } else if starts(&["STM32F0", "STM32F3"]) && obr & (1 << 2) != 0 {
                2
            } else {
                1
            }))
        }
        // STM32L5: RDP is the low byte of FLASH_OPTR at +0x40. 0x55 with TrustZone on is level
        // 0.5, which limits debugging, so it is reported as protected.
        Some(Unlock::OptionRegisterNs(base)) => {
            let optr = core.read_word_32(base + RegLayout::L5.optr).map_err(e)?;
            Ok(Some(match optr & 0xFF {
                0xAA => 0,
                0xCC => 2,
                _ => 1,
            }))
        }
        // STM32H7: RDP is bits 15:8 of FLASH_OPTSR_CUR.
        Some(Unlock::OptionStart(base)) => {
            let optsr = core.read_word_32(base + 0x1C).map_err(e)?;
            Ok(Some(match (optsr >> 8) & 0xFF {
                0xAA => 0,
                0xCC => 2,
                _ => 1,
            }))
        }
        // FLASH_USD bit 1 (FAP) is set while the Artery chip is protected.
        Some(Unlock::UsdErase { flash, .. }) => {
            let usd = core.read_word_32(flash + 0x1C).map_err(e)?;
            Ok(Some(if usd & (1 << 1) != 0 { 1 } else { 0 }))
        }
        // The RDP byte is the low byte of FLASH_OPTR (L4 family) or FLASH_OBR (L0/L1):
        // 0xAA = level 0, 0xCC = level 2, anything else = level 1.
        Some(Unlock::OptionRegister(base)) | Some(Unlock::Eeprom(base)) => {
            let offset = if matches!(unlock_method(chip), Some(Unlock::Eeprom(_))) { 0x1C } else { 0x20 };
            let value = core.read_word_32(base + offset).map_err(e)?;
            Ok(Some(match value & 0xFF {
                0xAA => 0,
                0xCC => 2,
                _ => 1,
            }))
        }
        _ => Ok(None),
    }
}

/// Only nRF and SAM D/E/L tell by their attach whether they are locked; for EFM32/EFR32 the
/// state cannot be seen this way.
fn mass_erase_shows_lock(chip: &str) -> bool {
    let name = chip.to_ascii_uppercase();
    ["NRF", "SAMD", "SAME", "SAML"].iter().any(|p| name.starts_with(p))
}

/// `capabilities`: what ColdHex can do about the read-out protection of this chip. Needs no
/// probe. {"event":"capabilities","status":true,"lock":true,"unlock":true}
fn run_capabilities(args: &Args) -> Result<(), String> {
    let chip = args.required("--chip")?.clone();
    let method = unlock_method(&chip);
    let status = match &method {
        None => false,
        Some(Unlock::MassErase) => mass_erase_shows_lock(&chip),
        Some(_) => true,
    };
    let lock = matches!(
        method,
        Some(Unlock::OptionControl | Unlock::OptionByteErase(_) | Unlock::UsdErase { .. } | Unlock::OptionRegister(_) | Unlock::OptionRegisterNs(_) | Unlock::OptionStart(_) | Unlock::Eeprom(_))
    );
    emit(json!({"event": "capabilities", "status": status, "lock": lock, "unlock": method.is_some()}));
    Ok(())
}

/// nRF, SAM D/E/L: probe-rs unlocks a locked chip while it attaches, but only with the
/// permission to erase everything. So an attach WITHOUT that permission shows the state: it
/// works on an open chip and fails with a message about `erase_all` on a locked one.
fn mass_erase_chip_is_locked(args: &Args) -> Result<bool, String> {
    match connect(args) {
        Ok(_) => Ok(false),
        Err(text) => {
            let lower = text.to_ascii_lowercase();
            if lower.contains("erase_all") || lower.contains("is locked") || lower.contains("locked") {
                Ok(true)
            } else {
                Err(text)
            }
        }
    }
}

/// `protection`: reports the read-out protection level as {"event":"protection","level":N}
/// (level is null when it cannot be told).
fn run_protection(args: &Args) -> Result<(), String> {
    let chip = args.required("--chip")?.clone();
    if matches!(unlock_method(&chip), Some(Unlock::MassErase)) {
        if !mass_erase_shows_lock(&chip) {
            emit(json!({"event": "protection", "level": Value::Null}));
            return Ok(());
        }
        let level = if mass_erase_chip_is_locked(args)? { 1 } else { 0 };
        emit(json!({"event": "protection", "level": level}));
        return Ok(());
    }
    let mut session = connect(args)?;
    let mut core = session.core(0).map_err(|e| format!("cannot access the core: {e}"))?;
    let level = read_protection(&mut core, &chip)?;
    emit(json!({"event": "protection", "level": level}));
    Ok(())
}

/// Reads `halfwords` 16-bit values starting at `address` (an even number of them).
fn read_halfwords(core: &mut probe_rs::Core, address: u64, halfwords: usize) -> Result<Vec<u16>, String> {
    let mut words = vec![0u32; halfwords / 2];
    core.read_32(address, &mut words).map_err(|e| format!("cannot read the option bytes: {e}"))?;
    Ok(words.iter().flat_map(|w| [*w as u16, (*w >> 16) as u16]).collect())
}

/// STM32F0/F1/F3 style lock. Setting RDP means erasing the whole option block, which also resets
/// the user, data and write-protection bytes: so the block is read first and written back
/// unchanged, except for RDP. The chip is not protected before this (the caller checks), so
/// the erase does not erase the flash. Returns false when the chip is protected already.
fn lock_option_byte_erase(core: &mut probe_rs::Core, key: u8) -> Result<bool, String> {
    const BASE: u64 = 0x4002_2000;
    const KEYR: u64 = BASE + 0x04;
    const OPTKEYR: u64 = BASE + 0x08;
    const SR: u64 = BASE + 0x0C;
    const CR: u64 = BASE + 0x10;
    const OBR: u64 = BASE + 0x1C;
    const BSY: u32 = 1 << 0;
    const OPTPG: u32 = 1 << 4;
    const OPTER: u32 = 1 << 5;
    const STRT: u32 = 1 << 6;
    const OPTWRE: u32 = 1 << 9;
    const OPTION_BYTES: u64 = 0x1FFF_F800;
    let e = |what: &str, err: probe_rs::Error| format!("{what}: {err}");

    let obr = core.read_word_32(OBR).map_err(|x| e("read FLASH_OBR", x))?;
    if obr & (1 << 1) != 0 {
        return Ok(false);
    }
    let mut block = read_halfwords(core, OPTION_BYTES, 8)?;
    if block[0] & 0xFF != u16::from(key) {
        return Err("the option bytes do not show the expected unprotected key; not changing them".to_string());
    }

    core.write_word_32(KEYR, FLASH_KEY1).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(KEYR, FLASH_KEY2).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(OPTKEYR, FLASH_KEY1).map_err(|x| e("option unlock", x))?;
    core.write_word_32(OPTKEYR, FLASH_KEY2).map_err(|x| e("option unlock", x))?;
    let cr = core.read_word_32(CR).map_err(|x| e("read FLASH_CR", x))?;
    if cr & OPTWRE == 0 {
        return Err("the option bytes could not be unlocked (FLASH_CR.OPTWRE stays 0)".to_string());
    }

    wait_not_busy(core, SR, BSY)?;
    core.write_word_32(CR, cr | OPTER).map_err(|x| e("write FLASH_CR", x))?;
    core.write_word_32(CR, cr | OPTER | STRT).map_err(|x| e("write FLASH_CR", x))?;
    wait_not_busy(core, SR, BSY)?;
    core.write_word_32(CR, cr & !OPTER).map_err(|x| e("write FLASH_CR", x))?;

    // RDP 0x00 (level 1 on every family here; 0xCC would be level 2 and is never written),
    // its complement 0xFF in the upper byte. Every other value goes back as it was.
    block[0] = 0xFF00;
    core.write_word_32(CR, cr | OPTPG).map_err(|x| e("write FLASH_CR", x))?;
    for (index, value) in block.iter().enumerate() {
        if *value == 0xFFFF {
            continue; // already what an erased halfword holds
        }
        core.write_word_16(OPTION_BYTES + 2 * index as u64, *value).map_err(|x| e("write the option bytes", x))?;
        wait_not_busy(core, SR, BSY)?;
    }
    core.write_word_32(CR, cr & !OPTPG).map_err(|x| e("write FLASH_CR", x))?;
    Ok(true)
}

/// Artery AT32 lock: the chip is protected whenever the FAP halfword is not 0xA5, so the user
/// system data is erased and everything except FAP is written back as it was. Returns false
/// when the chip is protected already.
fn lock_usd(core: &mut probe_rs::Core, flash: u64, usd: u64, halfwords: usize) -> Result<bool, String> {
    let keyr = flash + 0x04;
    let usd_unlock = flash + 0x08;
    let sts = flash + 0x0C;
    let ctrl = flash + 0x10;
    let usd_reg = flash + 0x1C;
    const OBF: u32 = 1 << 0;
    const USDPRGM: u32 = 1 << 4;
    const USDERS: u32 = 1 << 5;
    const ERSTR: u32 = 1 << 6;
    const USDULKS: u32 = 1 << 9;
    const FAP: u32 = 1 << 1;
    let e = |what: &str, err: probe_rs::Error| format!("{what}: {err}");

    if core.read_word_32(usd_reg).map_err(|x| e("read FLASH_USD", x))? & FAP != 0 {
        return Ok(false);
    }
    let block = read_halfwords(core, usd, halfwords)?;

    core.write_word_32(keyr, FLASH_KEY1).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(keyr, FLASH_KEY2).map_err(|x| e("flash unlock", x))?;
    core.write_word_32(usd_unlock, FLASH_KEY1).map_err(|x| e("user data unlock", x))?;
    core.write_word_32(usd_unlock, FLASH_KEY2).map_err(|x| e("user data unlock", x))?;
    let control = core.read_word_32(ctrl).map_err(|x| e("read FLASH_CTRL", x))?;
    if control & USDULKS == 0 {
        return Err("the user system data could not be unlocked (FLASH_CTRL.USDULKS stays 0)".to_string());
    }

    wait_not_busy(core, sts, OBF)?;
    core.write_word_32(ctrl, control | USDERS).map_err(|x| e("write FLASH_CTRL", x))?;
    core.write_word_32(ctrl, control | USDERS | ERSTR).map_err(|x| e("write FLASH_CTRL", x))?;
    wait_not_busy(core, sts, OBF)?;
    core.write_word_32(ctrl, control & !USDERS).map_err(|x| e("write FLASH_CTRL", x))?;

    // FAP (index 0) stays erased = protected; the rest goes back.
    core.write_word_32(ctrl, control | USDPRGM).map_err(|x| e("write FLASH_CTRL", x))?;
    for (index, value) in block.iter().enumerate().skip(1) {
        if *value == 0xFFFF {
            continue;
        }
        core.write_word_16(usd + 2 * index as u64, *value).map_err(|x| e("write the user system data", x))?;
        wait_not_busy(core, sts, OBF)?;
    }
    core.write_word_32(ctrl, control & !USDPRGM).map_err(|x| e("write FLASH_CTRL", x))?;
    Ok(true)
}

/// `lock`: sets the read-out protection to level 1 (never level 2).
fn run_lock(args: &Args) -> Result<(), String> {
    let chip = args.required("--chip")?.clone();
    let method = match unlock_method(&chip) {
        Some(m @ (Unlock::OptionControl | Unlock::OptionByteErase(_) | Unlock::UsdErase { .. } | Unlock::OptionRegister(_) | Unlock::OptionRegisterNs(_) | Unlock::OptionStart(_) | Unlock::Eeprom(_))) => m,
        _ => return Err(format!("locking is not supported for {chip} yet")),
    };
    let mut session = connect(args)?;
    let mut core = session.core(0).map_err(|e| format!("cannot access the core: {e}"))?;
    let changed = match method {
        Unlock::OptionControl => lock_option_control(&mut core)?,
        Unlock::OptionByteErase(key) => lock_option_byte_erase(&mut core, key)?,
        Unlock::UsdErase { flash, usd, halfwords } => lock_usd(&mut core, flash, usd, halfwords)?,
        Unlock::OptionRegister(base) => set_rdp_register(&mut core, base, RDP_LEVEL1)?,
        Unlock::OptionRegisterNs(base) => set_rdp_optr(&mut core, base, RegLayout::L5, RDP_LEVEL1)?,
        Unlock::OptionStart(base) => set_rdp_h7(&mut core, base, RDP_LEVEL1)?,
        Unlock::Eeprom(base) => set_rdp_eeprom(&mut core, base, false)?,
        _ => unreachable!(),
    };
    if !changed {
        emit(json!({"event": "message", "text": "The chip is already read-protected; nothing was changed."}));
        return Ok(());
    }
    let _ = core.reset();
    emit(json!({"event": "message", "text": "Read protection level 1 is set. Disconnect the power of the board for a moment; the flash cannot be read afterwards."}));
    Ok(())
}

/// `unlock`: removes the read-out protection (level 1). The chip erases its whole flash while
/// it does so. A level 2 chip cannot be unlocked and reports an error.
fn run_unlock(args: &Args) -> Result<(), String> {
    let chip = args.required("--chip")?.clone();
    let method = unlock_method(&chip)
        .ok_or_else(|| format!("no unlock method is known for {chip} yet"))?;

    // The chip's own mass erase would wipe an open chip too: only go on when it is locked.
    // SAM chips must be unlocked while connected under reset.
    let mut args = args.clone();
    if matches!(method, Unlock::MassErase) {
        if chip.to_ascii_uppercase().starts_with("SAM") && !args.flag("--connect-under-reset") {
            args.flags.push("--connect-under-reset".to_string());
        }
        if mass_erase_shows_lock(&chip) && !mass_erase_chip_is_locked(&args)? {
            emit(json!({"event": "message", "text": "The chip is not read-protected."}));
            return Ok(());
        }
    }
    let args = &args;

    // Mass erase through probe-rs needs the permission to erase everything.
    let mut session = connect_with(args, Permissions::new().allow_erase_all())?;

    match method {
        Unlock::MassErase => {
            let mut progress = FlashProgress::new(report);
            emit(json!({"event": "message", "text": "Erasing the whole chip through the debug port..."}));
            flashing::erase_all(&mut session, &mut progress, false)
                .map_err(|e| format!("unlock failed: {e}"))?;
        }
        Unlock::OptionByteErase(_) | Unlock::UsdErase { .. } | Unlock::OptionControl | Unlock::OptionRegister(_) | Unlock::OptionRegisterNs(_) | Unlock::OptionStart(_) | Unlock::Eeprom(_) => {
            let mut core = session.core(0).map_err(|e| format!("cannot access the core: {e}"))?;
            match method {
                Unlock::OptionByteErase(key) => unlock_option_byte_erase(&mut core, key)?,
                Unlock::UsdErase { flash, usd, .. } => {
                    if !unlock_usd_erase(&mut core, flash, usd)? {
                        emit(json!({"event": "message", "text": "The chip is not read-protected."}));
                        return Ok(());
                    }
                }
                Unlock::OptionRegisterNs(base) => {
                    if !set_rdp_optr(&mut core, base, RegLayout::L5, 0xAA)? {
                        emit(json!({"event": "message", "text": "The chip is not read-protected."}));
                        return Ok(());
                    }
                }
                Unlock::OptionStart(base) => {
                    if !set_rdp_h7(&mut core, base, 0xAA)? {
                        emit(json!({"event": "message", "text": "The chip is not read-protected."}));
                        return Ok(());
                    }
                }
                Unlock::OptionRegister(base) => {
                    if !set_rdp_register(&mut core, base, 0xAA)? {
                        emit(json!({"event": "message", "text": "The chip is not read-protected."}));
                        return Ok(());
                    }
                }
                Unlock::Eeprom(base) => {
                    if !set_rdp_eeprom(&mut core, base, true)? {
                        emit(json!({"event": "message", "text": "The chip is not read-protected."}));
                        return Ok(());
                    }
                }
                _ => unlock_option_control(&mut core)?,
            }
            // The new setting is read from the option bytes at the next power-on reset.
            let _ = core.reset();
        }
    }
    emit(json!({"event": "message", "text": "Unlocked. Disconnect the power of the board for a moment, then connect again."}));
    Ok(())
}

fn main() {
    let raw: Vec<String> = std::env::args().skip(1).collect(); // skip the program name
    let Some(command) = raw.first().cloned() else {
        emit(json!({"event": "result", "ok": false, "error": "no command (use erase, download, verify, read, lock, unlock or info)"}));
        std::process::exit(1);
    };
    let args = parse_args(&raw[1..]);

    let outcome = match command.as_str() {
        "erase" => run_erase(&args),
        "download" => run_download(&args),
        "verify" => run_verify(&args),
        "read" => run_read(&args),
        "info" => run_info(&args),
        "unlock" => run_unlock(&args),
        "lock" => run_lock(&args),
        "protection" => run_protection(&args),
        "capabilities" => run_capabilities(&args),
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unlock_method_follows_the_chip_family() {
        assert!(matches!(unlock_method("STM32F103C8Tx"), Some(Unlock::OptionByteErase(0xA5))));
        assert!(matches!(unlock_method("APM32F103CB"), Some(Unlock::OptionByteErase(0xA5))));
        assert!(matches!(unlock_method("STM32F407VGTx"), Some(Unlock::OptionControl)));
        assert!(matches!(unlock_method("GD32F450ZK"), Some(Unlock::OptionControl)));
        assert!(matches!(unlock_method("nRF52840_xxAA"), Some(Unlock::MassErase)));
        assert!(matches!(unlock_method("ATSAMD21G18A"), None));
        assert!(matches!(unlock_method("STM32L476RG"), Some(Unlock::OptionRegister(0x4002_2000))));
        assert!(matches!(unlock_method("STM32WB55CC"), Some(Unlock::OptionRegister(0x5800_4000))));
        assert!(matches!(unlock_method("STM32L073RZ"), Some(Unlock::Eeprom(0x4002_2000))));
        assert!(matches!(unlock_method("STM32L152RE"), Some(Unlock::Eeprom(0x4002_3C00))));
        // Families without a verified recipe stay unsupported instead of guessing.
        assert!(matches!(unlock_method("STM32F051R8"), Some(Unlock::OptionByteErase(0xAA))));
        assert!(matches!(unlock_method("AT32F403ACGU7"), Some(Unlock::UsdErase { flash: 0x4002_2000, usd: 0x1FFF_F800, .. })));
        assert!(matches!(unlock_method("AT32F437ZMT7"), Some(Unlock::UsdErase { usd: 0x1FFF_C000, .. })));
        assert!(matches!(unlock_method("PY32F030F16U6TR"), Some(Unlock::OptionRegister(0x4002_2000))));
        assert!(unlock_method("PY32F002B").is_none());
        assert!(matches!(unlock_method("STM32H743ZITx"), Some(Unlock::OptionStart(0x5200_2000))));
        assert!(matches!(unlock_method("STM32L552ZETxQ"), Some(Unlock::OptionRegisterNs(0x4002_2000))));
        assert!(unlock_method("STM32H7R7I8").is_none());
        assert!(unlock_method("STM32U575ZI").is_none());
        assert!(unlock_method("STM32H563ZI").is_none());
        assert!(unlock_method("STM32WB05KZ").is_none());
    }
}
