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
Projects are stored in `systems/` at the repo root (gitignored).

## System Project Layout

Each project in `systems/` is a self-contained machine description:

```
systems/<project-name>/
  system.json             Composer source (topology + paths)
  anolis-runtime.yaml     Generated runtime config
  providers/
    <id>.yaml             Generated per-provider config
  behaviors/              Hand-authored BT XMLs for this system (optional, v1+)
    main.xml
  logs/
    latest.log            Ephemeral launch log (gitignored)
  running.json            Ephemeral launch state (gitignored)
```

Contract notes:

- `system.json` is the composer source of truth. Generated YAML files are derived output.
- `topology.runtime.behavior_tree_path` is the composer-facing BT field; generated runtime YAML uses the canonical runtime key `automation.behavior_tree`.
- `topology.runtime.telemetry` mirrors the runtime's nested `telemetry.*` structure. The composer keeps telemetry settings in `system.json` even when telemetry is disabled, but the generated runtime YAML only emits the nested `telemetry.influxdb` block when telemetry is enabled.

The `behaviors/` subdirectory is not managed by the composer. Place BT XMLs
there manually and reference them by path in `behavior_tree_path`. The repo-level
`behaviors/` directory is reserved for generic and test BTs not associated with
any specific system.

## Out of Scope

**Behavior tree authoring is not part of this tool and never will be.**
Use [Groot2](https://www.behaviortree.dev/groot/) to create and edit BT XMLs —
it is the purpose-built editor for BehaviorTree.CPP trees, with a graphical
canvas, node palette, and live monitoring. The composer's only role with respect
to BTs is storing the path to an XML file and passing it to the runtime at launch.
