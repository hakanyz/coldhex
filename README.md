# ProbeDesk

ProbeDesk is a modern, modular desktop flashing and management utility for ARM Cortex-M microcontrollers (STM32, GD32, Geehy/APM32, TI, Nuvoton, and others), independent of any single silicon vendor.

## Key Features

- **Broad Microcontroller Ecosystem:** CMSIS-Pack powered support for Cortex-M families (STM32, GD32, Geehy, TI, Nuvoton, etc.)
- **Hardware Debug Probe Support:** J-Link, RailLink, and CMSIS-DAP
- **probe-rs Engine:** Robust, open-source, Rust-based asynchronous flashing back-end
- **Dual-Slot Flashing:** Coordinated sequential programming & verification for Bootloader and Application binaries
- **Auto-Speed Fallback:** Dynamic frequency reduction when communication or verification errors occur
- **Layered Architecture:** Clear decoupling between Presentation (Qt Widgets), Core logic, and Engine drivers

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
