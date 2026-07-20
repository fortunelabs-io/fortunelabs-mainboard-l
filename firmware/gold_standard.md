# Fortune Labs Mainboard — Firmware Gold Standard

> **Status:** living document. This is the architectural constitution the codebase
> is judged against. When code and this document disagree, *one of them is wrong* —
> resolve it explicitly, don't let drift accumulate.
>
> **Scope:** firmware layer (`firmware/main/`), ESP-IDF v5.3.2 + FreeRTOS.
> **Last decided:** see "Decision Log" at the bottom. Items marked **OPEN** are
> not yet binding and must not be enforced in review until resolved.

---

## 0. How to read this document

Each rule is either **FIXED** (already settled, derived from the codebase or a
made decision) or **OPEN** (deliberately undecided — recorded so we don't
pretend a default is a decision).

A rule being FIXED does not mean the current code obeys it. Where current code
violates a FIXED rule, that is a *finding*, tracked separately in the findings
document — not here.

---

## 1. Layering

The firmware is organized into strict layers. Dependencies point **downward only**.
A lower layer must never `#include` or call into a higher one.

```
  app_main / orchestration   (main.c)
        │  owns lifetimes, selects concrete drivers, wires everything
        ▼
  tasks/                      (task_sensor, task_actuator, task_display, …)
        │  business logic; depends on HAL contracts, not concrete ICs
        ▼
  hal/                        (sensor_driver, output_driver, display_driver, transport_driver)
        │  vtable contracts — the polymorphism boundary
        ▼
  drivers/                    (ic/, sensor/, output/) — concrete implementations
        │  IC drivers + HAL adapters
        ▼
  bus/                        (i2c_bus) — shared platform infrastructure
        │
  system/                     (config, log, ota, supervisor) — cross-cutting services
```

**FIXED rules:**

- A driver must never reach upward into a task or into `app_types.h` for queues.
  Cross-layer communication upward happens through **callbacks injected at init**,
  never through a global the lower layer pulls in. (See §5.)
- `bus/i2c_bus` is fixed platform infrastructure: direct function calls, **no vtable**.
  There is no substitution scenario, so an interface contract there is premature.
- System services (`system/*`) are cross-cutting and may be called from any layer,
  but must not themselves depend on tasks or HAL adapters.

---

## 2. Driver patterns

This is the most load-bearing section. Three distinct things in this codebase have
all been called "driver" or "vtable" at various points. They are **not** the same
and must not be conflated.

### 2.1 IC driver (fixed-BOM, single implementation) — **MCP23017 pattern** *(FIXED, Decision #1)*

For any IC the ESP32 talks to over a bus where the part is fixed in the BOM and has
no substitution case (ADS1115, MCP23017, and all future ICs of this kind):

- **No internal vtable, no `*_get_driver()`.** Expose plain public functions.
- **Static helpers** for internal register access (`_write_reg`, `_read_reg`, guards).
  Hiding statics from external callers is achieved by `static`, not by an indirection.
- **Per-instance context struct** allocated by the caller (`mcp23017_t`, `ads1115_dev_t`).
  Every public function takes `*dev` as its first argument. No file-scope state.
- **Clock speed comes from config** (`config->scl_hz`), never hardcoded in the driver.
- Register map → enums → config struct → runtime context struct → public functions.
  Concrete implementation follows the datasheet, not a rigid checklist.

> **Rationale:** MCP23017 proves that "single access path + hidden internals" is
> fully achieved with `static` + public functions. The internal vtable in ADS1115
> bought nothing that `static` didn't already provide, at the cost of an extra
> indirection and a `get_driver()` ceremony. The vtable is reserved for the HAL
> boundary (§2.3), where polymorphism is real.

> **Consequence:** ADS1115's `ads1115_driver_t` + `ads1115_get_driver()` are
> **non-conforming** and slated to be refactored to this pattern. Tracked in findings.

### 2.2 HAL adapter (concrete driver → HAL contract)

A thin layer that wraps an IC driver (or native peripheral) behind a HAL vtable so
the task layer can stay IC-agnostic. Example targets: a `relay` adapter wrapping
MCP23017 behind `output_driver_t`; an ADS1115 adapter behind `sensor_driver_t`.

- Implements the HAL vtable for its peripheral *class*.
- Holds the concrete driver's context internally; translates HAL calls into driver calls.
- This is where "swap the part, keep the business logic" actually lives.

### 2.3 HAL contract (the polymorphism boundary) — vtable **is** correct here *(FIXED)*

`sensor_driver_t`, `output_driver_t`, `display_driver_t`, `transport_driver_t`.

- These are genuine interface contracts. The vtable earns its indirection because
  the task layer depends on the contract and the implementation is meant to vary.
- Exactly one centralized Doxygen block above the typedef documents all params/returns
  used collectively by the function pointers (the established house style — §6).

### 2.4 SSD1306 / display — merged IC+adapter, singleton — **accepted exception** *(FIXED, Decision #2)*

SSD1306 fuses the IC driver and the `display_driver_t` adapter into one file, and uses
file-scope singleton state rather than a per-instance context.

- **Accepted as a deliberate exception**, not the general pattern.
- **Recorded rationale:** the board has no built-in display; the OLED is an optional/
  external single unit. There will never be two displays on this board, so the
  per-instance machinery and a separate adapter layer would be ceremony without payoff.
- **Boundary of the exception:** if a daughter board ever introduces a second display,
  this collapses and SSD1306 must be split into IC-pure + adapter and de-singletoned.
- Sensor and output paths do **not** get this exception — they follow §2.1 + §2.2.

---

## 3. Task layer & dependency injection *(FIXED, Decision #3)*

**The HAL line is a containment boundary.** Everything hardware-concrete — the
"dirty space" of register maps, IC quirks, bus calls, concrete vtables — stops
*below* the HAL line. A task lives *above* it and may only ever see an abstraction.

**FIXED rules:**

- A task **must not** `#include` any concrete driver header (`*_dummy.h`,
  `ssd1306.h`, `ads1115.h`, …). The only driver-facing include a task may have is the
  HAL contract (`hal/sensor_driver.h`, etc.).
- A task depends **only** on a HAL contract pointer (`const sensor_driver_t *`,
  `const output_driver_t *`, `const display_driver_t *`). It never holds, sees, or
  calls a raw IC-driver context.
- The HAL pointer is **injected from outside** — passed in at task creation via the
  task's parameter (or an init call). `main.c` owns the *selection* of which concrete
  driver/adapter is wired in. Swapping the implementation = changing that one wiring
  point in `main.c`, with zero edits to task code.
- Raw IC drivers (§2.1) may be used directly **only** below the HAL line — inside HAL
  adapters (§2.2) or in `main.c` for one-off boot validation. Never inside a task.

> **Rationale:** this is what makes the platform a platform. The business logic in a
> task is the reusable asset across board variants and clients; it stays clean only if
> the hardware specifics can never leak into it. The claim already written in
> `sensor_driver.h` ("swap = change one pointer in main, zero changes to business
> logic") is hereby made binding, not aspirational.

> **Consequence:** current tasks (`task_sensor`, `task_actuator`, `task_display`) all
> `#include` concrete driver headers and call their global vtables by name —
> **non-conforming**, tracked in findings. Each needs a HAL pointer threaded in from
> `main.c`.

---

## 4. System health & watchdog *(FIXED, Decision #6)*

- **TWDT scope: supervisor task only.** The Task Watchdog is treated as proof that
  the scheduler is alive, *not* as per-task liveness monitoring.
- Worker tasks (`task_sensor`, `task_actuator`, `task_display`, telemetry) **do not**
  register with or feed the watchdog.
- `task_supervisor` registers itself and self-feeds at a safe sub-timeout cadence.
- **Consequence:** a hung worker task will *not* trip the watchdog by design. If
  per-task liveness is ever wanted, that is a new decision, not a bug to be silently
  patched.

---

## 5. Cross-task & cross-layer communication

- **FIXED:** inter-task data flow is via **FreeRTOS queues**. Queues are created in
  `main.c` and exposed through `app_types.h`.
- **FIXED (Decision #5):** strict purity is *not* required for the current PoC/mock
  stage. A read-only cross-task getter for a single value (e.g.
  `task_sensor_get_latest_voltage()`) is tolerated as a pragmatic shortcut while the
  sensor path is still a placeholder. It is **not** the endorsed pattern for production
  data flow and should not proliferate. Revisit when the real sensor path is wired.
- **OPEN (Decision #4):** whether a *system service* updating the display must go
  through an injected callback (mirroring `sys_health_publish_fn` in the supervisor)
  or may push to `g_queue_display` directly. Currently `system_ota` pushes directly.
  **Not enforced until decided.** Until then, do not flag OTA's direct queue push as a
  violation — flag it only as an inconsistency awaiting this decision.

---

## 6. Documentation standard *(FIXED)*

Doxygen/XML, matching the established house style.

- **Centralized block above a struct/vtable typedef** documenting *all* params and
  returns used collectively by the inner members. (`transport_driver_t`,
  `ads1115_driver_t`, the HAL contracts are the reference exemplars.)
- `@brief` — 1–2 line summary of purpose.
- `@param` for every input; `@return` enumerating concrete outcomes
  (`ESP_OK`, specific `esp_err_t`, `true`/`false`, or `void`).
- **Internal static helpers are documented to the same bar as public functions.**
  MCP23017's helpers (`_mcp23017_write_reg`, etc., with full `@param`/`@return`) are
  the gold standard. ADS1115's one-line helper comments are **below** standard.
- Comments stay cleanly aligned.
- Documentation must describe the **current** state of the code. A doc block that
  describes a past state or a "should refactor to…" that has already happened is a
  defect (see the stale `ARCHITECTURAL_NOTE` in `system_ota.h`).

---

## 7. Code style conventions *(FIXED)*

- **Naming:** ESP-IDF conventions. System modules use the `system_*` prefix.
- **Error handling:** return `esp_err_t`; validate args first and return
  `ESP_ERR_INVALID_ARG` / `ESP_ERR_INVALID_STATE` before doing work. `void` only for
  deinit-style calls that genuinely can't fail.
- **`const` correctness:** config pointers passed in are `const`; honor it.
- **Explicit casts** where width/sign conversion happens.
- **Header guards:** every header wraps its declarations in
  `#ifdef __cplusplus / extern "C" { … } #endif`, with the closing brace **inside**
  the same guard. (`transport_driver.h`'s unguarded closing brace is a defect.)
- **Include paths (Decision #7):** root-relative, MCP23017 style —
  `"drivers/ic/ads1115.h"`, `"bus/i2c_bus.h"`. Not file-local (`"mcp23017.h"`).
- **Internal helper naming (Decision #8):** leading underscore for *all* internal
  static helpers — `_mcp23017_write_reg`, `_ads1115_build_config_word`, etc.
  ADS1115 and i2c_bus helpers are to be renamed to conform.
- **CMake:** every new `.c` must be added to `SRCS`. `compile_commands.json` (and thus
  IntelliSense) requires a successful `idf.py build` first.

---

## 8. Platform vs. client code *(FIXED)*

- The platform layers (`system/`, `bus/`, `hal/`, `network/`) are the moat — they stay
  stable and are reusable across board variants (e.g. a future FLB-LP).
- Concrete sensor/actuator implementations are client-specific and are expected to be
  cloned/swapped per deployment; they must depend only on the HAL contracts.
- Current `*_dummy` drivers and the boot-time single-shot ADS1115 read are explicit PoC
  placeholders, not the production path.

---

## Decision Log

| # | Topic | Decision | Status |
|---|-------|----------|--------|
| 1 | IC driver pattern | MCP23017 pattern (public fns + static helpers, per-instance, no internal vtable) | FIXED |
| 2 | SSD1306 merged IC+adapter singleton | Accepted exception; board has no built-in display, single optional unit | FIXED |
| 3 | Task ↔ driver coupling | Task depends only on injected HAL pointer; no concrete/raw driver in task layer; main selects | FIXED |
| 4 | System service → display | Callback vs. direct queue push | **OPEN** |
| 5 | Cross-task data sharing | Queues are the rule; read-only getter tolerated for mock stage only | FIXED |
| 6 | TWDT scope | Supervisor task only; workers not watched, by design | FIXED |
| 7 | Include paths | Root-relative (MCP23017 style) | FIXED |
| 8 | Internal helper naming | Leading underscore for all static helpers | FIXED |
