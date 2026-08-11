# FortuneLabs Mainboard-L

Monorepo for the FortuneLabs Mainboard-L: the board itself, the firmware that
runs on it, and the host-side software that talks to it. Each arm lives in its
own top-level directory and carries its own toolchain and documentation.

## Layout

| Directory   | Contents                                                  | Toolchain          |
| ----------- | --------------------------------------------------------- | ------------------ |
| `hardware/` | KiCad schematic, PCB, and BOM for `fortunelabs_mainboard_v0` | KiCad 6+         |
| `firmware/` | ESP-IDF / FreeRTOS application for the ESP32-S3            | ESP-IDF, PlatformIO |
| `software/` | Host-side tooling and services                             | n/a                |

`fortunelabs.code-workspace` is a multi-root VS Code workspace covering the
firmware and software arms. Note that it hardcodes absolute paths for the
compiler and IDF include roots, so adjust it to your machine before use.

## Firmware

Requires the ESP-IDF environment on your `PATH` (`. $IDF_PATH/export.sh`).

```bash
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Note that the committed `sdkconfig` still targets `esp32` while the board carries
an ESP32-S3-WROOM-2; see the deviations listed in
[firmware/Readme.md](firmware/Readme.md). Host-native unit tests run under Unity
via PlatformIO and need no hardware:

```bash
cd firmware
pio test -e native
```

See [firmware/Readme.md](firmware/Readme.md) for the layer map, driver
conventions, and configuration details.

## Hardware

Open `hardware/fortunelabs_mainboard_v0/fortunelabs_mainboard_v0.kicad_pro` in
KiCad. See
[README-kicad-git-workflow.md](hardware/fortunelabs_mainboard_v0/README-kicad-git-workflow.md)
for the git conventions this project uses for KiCad files.

## Continuous Integration

[`.github/workflows/build.yml`](.github/workflows/build.yml) runs the ESP-IDF
firmware build and the native Unity test suite on every push to `main` and on
every pull request. A separate KiCad workflow lives under the hardware
directory. CI never invokes `flash` or `monitor`, because both require a physical
board and a serial TTY.

## Working conventions

[`todos/prototyping_todo.md`](todos/prototyping_todo.md) is the specification:
ten phases, each with a testable deliverable, plus the Decision Log and the Risk
Tracker. Status is never written into it; that lives in the issue tracker.

- [`docs/sop/issue_sop.md`](docs/sop/issue_sop.md): what gets an issue, the four
  issue types, and the rule that a gate is opened *before* the run that tests it.
- [`docs/sop/git_sop.md`](docs/sop/git_sop.md): branches, commit types, tags,
  and when a decision is owed an [ADR](docs/adr/README.md).

## License

Apache-2.0. See [LICENSE](LICENSE). Third-party components retain their own
licenses; `firmware/components/ssd1306/` is MIT-derived. See [NOTICE](NOTICE)
for the full attribution.
