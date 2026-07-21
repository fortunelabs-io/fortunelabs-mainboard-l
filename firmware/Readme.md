# Fortune Labs Mainboard Firmware

ESP-IDF and FreeRTOS firmware for the Fortune Labs Mainboard (ESP32-S3).

## Requirements

| Component    | Version                   |
| ------------ | ------------------------- |
| Target chip  | ESP32-S3                  |
| ESP-IDF      | v5.3.2                    |
| Build system | CMake, driven by `idf.py` |

## Building

```bash
idf.py set-target esp32s3
idf.py build
```

## Flashing and Monitoring

Run from the `firmware/` project root with the ESP-IDF environment activated
(`. $IDF_PATH/export.sh`). The simplest form lets the tool auto-detect the port:

```bash
idf.py flash monitor
```

To name the port explicitly, list what is connected first:

```bash
ls /dev/ttyACM* /dev/ttyUSB*
```

The port name depends on the board's USB-serial chip: a CH343/CH9102 enumerates as
`/dev/ttyACM0`, while a CH340 or CP210x enumerates as `/dev/ttyUSB0`. Pass the
matching node:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Exit the monitor with `Ctrl+]`.

## Configuration

Build-time options are managed through Kconfig:

```bash
idf.py menuconfig
```

Runtime configuration is persisted in NVS, with Kconfig as fallback.

## Repository Layout

```
main/
  main.c        Orchestration: task wiring, driver instance selection
  hal/          HAL contracts
  tasks/        Task layer
  drivers/      Driver implementations (sensor, output, ic)
  bus/          Platform infrastructure (i2c_bus)
  system/       Config, logging, supervisor, OTA
  network/      WiFi and connectivity

components/
  ssd1306/      Display driver component
```

## Architecture

This firmware follows a layered architecture with a strict hardware abstraction boundary between the task layer and concrete drivers. The full set of design invariants, the layer map, and the rules for adding a new driver or HAL contract are documented in `GOLD_STANDARD.md`.

## Status

See `task_list.md` for the current development phase and `FINDINGS.md` for known issues and deviations.
