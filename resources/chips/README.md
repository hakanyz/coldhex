# Bundled chip descriptions

Every `*.yaml` file in this folder is built into the ProbeDesk executable and loaded at
startup, so the chips in it are available without adding files by hand.

The files are probe-rs chip descriptions generated from vendor CMSIS-Packs:

    target-gen pack <vendor>.pack <output-folder>

and contain flash algorithms taken from those packs. Check the pack's licence before
committing generated files to a public repository; `*.yaml` here is git-ignored by default.
Re-run CMake after adding or removing files.
