# ProbeDesk

ProbeDesk is a modern, modular desktop flashing and management utility for ARM Cortex-M microcontrollers (STM32, GD32, Geehy/APM32, TI, and others), independent of any single silicon vendor.

## Key Features

- **Broad Microcontroller Ecosystem:** CMSIS-Pack powered support for Cortex-M families (STM32, GD32, Geehy, TI, etc.)
- **Hardware Debug Probe Support:** J-Link, ST-Link, and CMSIS-DAP
- **probe-rs Engine:** Robust, open-source, Rust-based asynchronous flashing back-end
- **Dual-Slot Flashing:** Coordinated sequential programming & verification for Bootloader and Application binaries
- **Auto-Speed Fallback:** Dynamic frequency reduction when communication or verification errors occur
- **Layered Architecture:** Clear decoupling between Presentation (Qt Widgets), Core logic, and Engine drivers

## Requirements

ProbeDesk drives the [probe-rs](https://probe.rs) command line tool. Install `probe-rs` and make sure it is on `PATH` (the installer's default `~/.cargo/bin` is also searched).

## Chips unknown to probe-rs (e.g. APM32)

probe-rs ships with its own chip database. For chips it does not know, generate a chip description from the vendor's CMSIS-Pack and add it in ProbeDesk:

1. Download the vendor's `.pack` file (for example from <https://www.keil.arm.com/packs/>).
2. Convert it with the `target-gen` tool from the probe-rs release page:
   `target-gen pack <vendor>.pack <output-folder>`
3. In ProbeDesk choose **Tools > Add Chip Description File...** and select a generated `.yaml` file.

The chips from that file then appear in the chip list (also under **Custom files**).

To ship chips inside the executable instead, put the generated `.yaml` files in `resources/chips/` and re-run CMake; they are loaded on every start (see `resources/chips/README.md`).

## Packages

Pushing to GitHub runs `.github/workflows/build.yml`, which builds, tests and packages ProbeDesk:

| Platform | Files |
|---|---|
| Windows | `probedesk-<version>-win64.zip` (portable), `probedesk-<version>-win64.exe` (installer) |
| Ubuntu 22.04 | `probedesk_<version>_amd64.deb`, `probedesk-<version>-Linux.tar.gz` |

Pushing a tag such as `v0.1.0` also attaches these files to a GitHub Release. probe-rs is downloaded
by the workflow and shipped inside every package. The `.deb` additionally installs USB access
rules so the probe can be used without root. On first start ProbeDesk checks probe-rs, the
connected probes and their drivers (Help > System Check) and explains anything that is missing.

To build a package locally, point CMake at a folder containing `probe-rs` and run CPack:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPROBEDESK_PROBE_RS_DIR=<folder with probe-rs>
cmake --build build
cd build && cpack
```

## Architecture Overview

```
src/
├── app/        # Application entry point & theme initialization
├── ui/         # Qt Widgets GUI (MainWindow, dialogs, logging console)
├── core/       # Flashing coordinator, state machine, HEX/BIN parsers
└── engine/     # Programmer engine interface & probe-rs driver
```

## License
MIT
