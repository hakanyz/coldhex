# Bundled chip descriptions

Every `*.yaml` file in this folder is built into the ColdHex executable and loaded at
startup, so the chips in it are available without adding files by hand.

The files are probe-rs chip descriptions generated from vendor CMSIS-Packs:

    target-gen pack <vendor>.pack <output-folder>

Re-run CMake after adding or removing files.
