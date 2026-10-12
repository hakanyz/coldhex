# Third-party software shipped with ColdHex

ColdHex's installers include the following components. Their licence texts are
available from the projects listed here or in this package.

## Qt 6 (GNU LGPL v3)

ColdHex uses the Qt toolkit (Qt Core, Gui, Widgets and the libraries they need) under the
terms of the GNU Lesser General Public License version 3. Qt is © The Qt Company Ltd. and
other contributors, and is dynamically linked: the Qt libraries are separate files
(`Qt6*.dll` / `libQt6*.so`) that you may replace with another build of the same Qt version.

- Licence texts: `Qt-LGPL-3.0.txt` and `GPL-3.0.txt` (the LGPL refers to the GPL)
- Qt source code: https://download.qt.io/official_releases/qt/ and https://code.qt.io
- The Windows packages are built with the Qt version named in `.github/workflows/build.yml`.
  The Linux packages use the Qt 6 libraries of the Linux distribution.
- More information: https://www.qt.io/licensing

## probe-rs (MIT or Apache-2.0)

The command line tools of probe-rs are dual-licensed under the MIT and Apache-2.0 licences
(`probe-rs-LICENSE-MIT`, `probe-rs-LICENSE-APACHE` in this package).
https://github.com/probe-rs/probe-rs

The `coldhex-flash` helper (`tools/coldhex-flash`) is linked with the probe-rs library and the
Rust crates it depends on (`tools/coldhex-flash/Cargo.lock` lists them with their versions).
probe-rs itself is covered by the licences above. The licences of the other crates still have to
be listed here before a public release.

## Chip descriptions for Geehy APM32 devices

The files in `resources/chips` were generated with probe-rs `target-gen` from Geehy's
CMSIS-Packs. https://www.keil.arm.com/packs/
