# Anolis System Composer

A graphical tool for composing, configuring, and launching Anolis systems —
no command-line knowledge required.

## Quick Start (Windows)

Double-click `start.cmd` from this directory. A browser window will open
automatically at `http://localhost:3002`.

## Quick Start (Linux / macOS)

```sh
./tools/system-composer/start.sh
```

## What it does

The composer lets you pick a starter template (sim, mixed-bus, bioreactor),
fill in device and provider settings through a form UI, save the system to a
named project, and launch the full Anolis runtime stack with one click.
Projects are stored in `tools/system-composer/systems/` (gitignored).
