# ProbeDesk

ProbeDesk is a modern, modular desktop flashing and management utility for ARM Cortex-M microcontrollers (STM32, GD32, Geehy/APM32, TI, Nuvoton, and others), independent of any single silicon vendor.

## Key Features

- **Broad Microcontroller Ecosystem:** CMSIS-Pack powered support for Cortex-M families (STM32, GD32, Geehy, TI, Nuvoton, etc.)
- **Hardware Debug Probe Support:** J-Link, ST-Link, and CMSIS-DAP
- **probe-rs Engine:** Robust, open-source, Rust-based asynchronous flashing back-end
- **Dual-Slot Flashing:** Coordinated sequential programming & verification for Bootloader and Application binaries
- **Auto-Speed Fallback:** Dynamic frequency reduction when communication or verification errors occur
- **Layered Architecture:** Clear decoupling between Presentation (Qt Widgets), Core logic, and Engine drivers

## Requirements

ProbeDesk drives the [probe-rs](https://probe.rs) command line tool. Install `probe-rs` and make sure it is on `PATH` (the installer's default `~/.cargo/bin` is also searched).

## Chips unknown to probe-rs (e.g. APM32, Nuvoton)

probe-rs ships with its own chip database. For chips it does not know, generate a chip description from the vendor's CMSIS-Pack and add it in ProbeDesk:

1. Download the vendor's `.pack` file (for example from <https://www.keil.arm.com/packs/>).
2. Convert it with the `target-gen` tool from the probe-rs release page:
   `target-gen pack <vendor>.pack <output-folder>`
3. In ProbeDesk choose **Tools > Add Chip Description File...** and select a generated `.yaml` file.

The chips from that file then appear in the chip list (also under **Custom files**).

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
