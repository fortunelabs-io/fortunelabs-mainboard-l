# Fortune Labs Firmware Architecture

## Preface

Hardware becomes expensive to change the moment it is fabricated. Firmware remains cheap to change until the moment it ships. This asymmetry is the reason firmware architecture is defined and enforced before the printed circuit board is finalized. Every decision recorded in this document exists to absorb uncertainty in the layer where uncertainty is still affordable to correct.

This document describes how firmware for the Fortune Labs Mainboard-L is structured, not how any single feature is implemented. It applies to all current and future firmware built on this platform, including board variants that do not yet exist.

A new file, driver, or task belongs in exactly one place. This document exists so that placement is decided by a fixed procedure, not by individual judgment applied fresh each time.

## How to Use This Document

Each section states a principle, the engineering reason behind it, and a test for applying it to a case not yet encountered. The principle is fixed. The test is what extends the principle to new situations without requiring the principle itself to be rewritten.

When a new case does not fit any existing test, a new principle is added following the procedure in Section 8. A principle is never bent silently to fit an exception. An exception is either covered by an existing test or it becomes a documented addition to this document.

## 1. Core Invariants

The following statements hold without exception. Every other section in this document expands one of these into a rationale and a test.

1. The task layer includes no concrete driver header.
2. A device with a realistic substitution path is accessed only through a HAL vtable.
3. A device without a substitution path still has a HAL adapter.
4. Platform infrastructure carries no vtable and is not treated as a device.
5. Device state has exactly one owning task at any time.
6. A driver returns raw data. Interpretation and unit conversion happen above the driver.
7. A driver reports failure. It does not decide recovery on the system's behalf.
8. Every new source file is added to CMakeLists.txt before the first build that uses it.
9. A task registered with the watchdog feeds it on every iteration, with no exception.
10. Loss of network connectivity is treated as expected external state, not as a firmware error.
11. A peripheral's known interference behavior with the radio subsystem is documented at the point that behavior is discovered.
12. Any deviation from an invariant above is written down at the point of deviation. Silence is not an exception, it is a defect.

## 2. Layer Map

Firmware is organized into six layers. Each layer may depend only on the layer directly below it.

```
Orchestration      main.c, task wiring, driver instance selection
Task                business logic (task_sensor, task_output, task_display, task_comm)
HAL contract        sensor_driver_t, output_driver_t, display_driver_t, transport_driver_t
HAL adapter         binds one concrete driver to a HAL contract
Driver              chip-specific implementation (ads1115, mcp23017, ssd1306)
Platform infra      i2c_bus, system_config, system_log
```

New code is placed by answering these questions in order.

1. Does the code react to a sensor reading, output command, display update, or transport event as part of system behavior? It belongs in the task layer.
2. Does the code speak a chip's register map or communication protocol? It belongs in the driver layer.
3. Does a task need to call this capability without knowing which chip provides it? A HAL contract is defined or extended, and a HAL adapter binds the driver to it.
4. Is the code a bus or service with no substitution scenario, used directly by drivers rather than tasks? It belongs to platform infrastructure.

If a question is unclear, Section 3 gives the exact test for the driver and HAL boundary.

The layers above describe communication within the board: firmware talking to devices it fully controls over a bus it owns. Communication across the board's edge, to a network or to a host machine over serial, is a separate concern with its own rules, covered in Section 6.

## 3. HAL Containment

**Principle**

The task layer depends only on HAL contract pointers. No concrete driver header is included above the HAL line.

**Rationale**

Every chip soldered onto the board is a decision that can change: a supplier discontinues a part, a cost target forces a substitution, a board revision requires a different sensor. Code that names a chip directly (`ads1115_read(...)`) inherits that fragility at every layer that calls it. Code that names a contract (`sensor_driver_t.read(...)`) does not. A task waiting on a sensor reading does not need to know, and must not need to know, whether that reading comes from an ADS1115, a simulated potentiometer, or a replacement chip that does not exist yet.

**Test**

For each new driver, ask: is there a realistic substitution path for this chip, now or in a future board variant?

- Yes: a HAL vtable and adapter are required. The task depends on the contract pointer, injected at initialization. See `sensor_driver_t`, `output_driver_t`, `display_driver_t`, and `transport_driver_t` for the current form of this pattern.
- No, but real I2C, SPI, or GPIO access is still involved: a HAL adapter is still required for containment. The vtable itself may be omitted. See Section 3.1.
- No, and the code is fixed platform infrastructure rather than a device: it sits below the HAL boundary entirely. See Section 3.2.

An exception produced by this test is not a gap in the rule. It is the rule working as intended.

### 3.1 When the Vtable Is Omitted

**Principle**

A vtable without a substitution path is overhead without benefit. The HAL adapter is still mandatory; the function pointer indirection is not.

**Rationale**

The SSD1306 is the platform's only optional external display, and no scenario exists in which it is replaced by a different chip without also changing `display_config_t` in a way that breaks the abstraction anyway. Forcing a vtable here adds one layer of indirection that is never called with a second implementation, by design.

**Test**

If a genuine substitution candidate for the chip appears later, this decision is reversed and a vtable is added at that time. Until then, a singleton binding is correct, not a shortcut.

### 3.2 The Lower Boundary of the HAL

**Principle**

`i2c_bus` and equivalent platform services sit below the HAL line. They are called directly by drivers, without a vtable, and are never called by tasks.

**Rationale**

HAL containment protects the task layer from knowing which device is attached. It does not protect a driver from knowing which bus it runs on, because no realistic version of this platform changes its physical bus mid-operation. `i2c_bus` is not a device that can be substituted. It is the ground every device stands on.

**Test**

If a module is called by drivers rather than tasks, and has no realistic substitution path (Section 3 test, third branch), it lives below the HAL line, not inside it.

## 4. State Ownership and Concurrency

**Principle**

Device state has exactly one owning task at any point in time. No mutable driver state is shared across tasks without either single-owner enforcement or an explicit mutex.

**Rationale**

FreeRTOS tasks execute concurrently, and a multi-step register operation on an I2C device is not atomic across that operation. A shadow-RAM read-modify-write on an expander, for example, is a sequence of steps that must not interleave with another task's access to the same device. Ownership discipline is what prevents that interleaving, and it is decided at design time, not discovered at debug time.

**Test**

For each stateful driver, ask: can two tasks call a mutating function on this driver concurrently?

- No: the driver's calls are restricted to a single owning task. This is the default and the preferred outcome.
- Yes: the shared state is protected by a mutex held for the shortest span possible, and never across a blocking I2C transaction.

### 4.1 Shadow State Caching

**Principle**

A driver may cache device register state locally only if the driver is the sole writer of that physical register.

**Rationale**

Caching avoids redundant I2C reads and makes pin operations fast, but a cache is only correct if nothing outside the driver can change the register it mirrors. An expander's shadow RAM is valid because the driver owns every write path to that register.

**Test**

Before adding a cache, confirm no other master, task, or interrupt can alter the physical register outside this driver's own write path. If any such path exists, the cache is treated as untrusted and re-synced from hardware rather than assumed correct.

## 5. Failure Propagation

**Principle**

A driver reports failure. It does not decide what happens next. Recovery policy lives above the driver that detected the failure.

**Rationale**

A driver that silently retries, resets a bus, or reboots on its own authority hides system state from every layer above it. Recovery decisions belong where system-wide context exists: whether the failure is momentary, whether the device is critical to safe operation, whether accumulated failures justify a supervised restart.

**Test**

When writing a driver function, ask: if this call fails, who decides what happens next?

- If the answer is the driver itself, the decision is moved up to the task or system layer.
- If the answer is the calling task, the driver returns the failure and the task decides.
- If the answer is system-wide (watchdog, OTA rollback), the failure is surfaced through logging and supervisor primitives already established for that purpose, not reinvented inside the driver.

### 5.1 Failure Classes

Failures are handled at the layer that matches their scope, never below it.

- Transient (a single bus error): the driver may retry within a small, explicitly documented budget, and still returns failure if the budget is exhausted. A retry that is not documented does not exist as far as this architecture is concerned.
- Persistent (a device that stops responding): the task layer decides how to represent the loss, whether to mark a reading invalid, and whether to continue operating in a degraded mode.
- Systemic (approaching a watchdog deadline, a failed firmware update): the system layer decides, using the supervisor and OTA rollback mechanisms built for that purpose.

## 6. External Boundary

The layers described in Sections 2 through 5 govern communication within the board: firmware talking to devices it fully controls over a bus it owns. Communication across the board's edge, to a WiFi access point, an MQTT broker, or a host machine over UART, does not answer to the same owner. The firmware does not control what is on the other end of that link, and the rules below exist because that difference has architectural consequences.

### 6.1 Network Reachability Is External State

**Principle**

Network connectivity is an external condition, not a firmware error. Code above the transport boundary treats an absent connection as an expected, ongoing state, not as an exception to be caught.

**Rationale**

A WiFi access point can be powered off, out of range, or congested for reasons that have nothing to do with firmware correctness. A design that treats disconnection as an error condition either blocks waiting for a state it cannot control, or fails loudly when the world does not cooperate. A design that treats disconnection as a normal state degrades cleanly while it lasts and recovers without intervention once the world cooperates again.

**Test**

For any code that depends on network reachability, ask what it does when the network is absent for an unbounded period. If the answer involves blocking, retry-until-success, or a reboot, the code is redesigned to skip its cycle and check again later, matching the standing pattern of the telemetry loop: read the connection flag, skip the publish if not ready, try again on the next tick.

### 6.2 Transport Follows the Same Substitution Test as Any Other Driver

**Principle**

The transport boundary is subject to the same containment test as a sensor or an output. A connectivity method with a realistic substitution path belongs behind `transport_driver_t`. A connectivity method with no substitution path may bypass the vtable, following the same reasoning as Section 3.1, but that decision is stated explicitly rather than left implicit.

**Rationale**

Manufacturing, agriculture, and utility deployments do not share one connectivity profile. A factory floor has WiFi. A remote field site may not. Where a second connectivity path (cellular, LoRaWAN) is a realistic outcome for a future board variant, the substitution path exists in principle even before it exists in code, and the current implementation is measured against that possibility rather than assumed permanent by default.

**Test**

Apply the Section 3 test directly to the transport boundary: is there a realistic substitution path for how this device reaches the outside world, now or for a future variant? If yes, the current implementation is brought into conformance with `transport_driver_t` or the gap is recorded explicitly as an open item. If no, the exception is written down the same way Section 3.1 writes down its own.

### 6.3 Shared Silicon Resource Contention

**Principle**

A peripheral that shares a clock or power domain with the radio subsystem has its interference behavior documented explicitly: which peripherals are affected, under what condition, and what the observed effect is.

**Rationale**

Radio transmit activity draws on shared chip resources in bursts. A peripheral whose timing depends on the same clock source can show timing drift during those bursts without any fault in that peripheral's own driver code. This is not a defect in the sense the earlier sections use the word: no invariant is violated, no data path is silently corrupted by incorrect logic. It is a hardware-level interaction that firmware accounts for through clock source selection, not through driver logic.

**Test**

When a peripheral shows symptoms during radio activity (framing errors, timing drift, corrupted output) with no corresponding fault in its own read or write logic, clock source is checked before a software defect is assumed. Where an independent clock source exists, decoupled from the clock the radio subsystem also draws on, it is selected for peripherals whose correctness matters during radio bursts, and the tradeoff of that choice (typically lower resolution or a fixed frequency) is recorded alongside the decision.

### 6.4 Diagnostic Channels Are Not Control Channels

**Principle**

A channel used only for local diagnostic output carries a different failure tolerance than a channel carrying telemetry or commands. Degradation on a diagnostic-only channel is not treated with the same urgency as degradation on a channel the system depends on to operate.

**Rationale**

Serial output read by a technician on a bench is not part of the system's control loop. Corruption there is a debugging inconvenience. The same corruption on a channel carrying commands or telemetry is a system fault. Treating both with equal urgency spends effort where it does not reduce risk, and can crowd out attention that a control-path fault needs first.

**Test**

Before prioritizing a fix on a communication path, identify whether the path is diagnostic-only or part of the control and telemetry path. A fault confined to a diagnostic-only path is scheduled behind any fault on a path that carries commands, telemetry, or safety-relevant data.

## 7. Naming as Layer Signal

**Principle**

A file's location declares its layer. A symbol is never placed somewhere its folder does not indicate.

**Rationale**

A layer boundary enforced only by convention in someone's memory does not survive the addition of a second contributor, or the passage of six months. A folder structure that mirrors the layer map (`hal/`, `drivers/ic/`, `drivers/sensor/`, `bus/`, `system/`, `tasks/`) makes the boundary visible without requiring the reader to already know the rules.

**Test**

If a file's folder and its actual dependency do not match (a file under `drivers/` that includes a task header, a file under `hal/` that names a concrete chip), the file is misplaced. Move it. Do not adjust the rule to fit the file.

## 8. Extending This Document

**Principle**

A new invariant is added only after a concrete case demonstrates the need for it. Speculative rules are not added in advance of a real case.

**Rationale**

A rule justified by imagination rather than a real failure or a real substitution tends to be wrong in a way that only shows up when the real case finally arrives, at which point it must be rewritten anyway. Waiting for the real case costs nothing. Guessing wrong costs a rewrite, plus every decision made on top of the wrong version in the meantime.

**Test**

Before adding a principle, confirm it was derived from an actual bug, an actual design conflict, or an actual hardware change, not a hypothetical one. State that case briefly as the rationale. Follow the same three-part form used throughout this document: principle, rationale, test.
