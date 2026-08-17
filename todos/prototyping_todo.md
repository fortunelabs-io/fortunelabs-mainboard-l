# Fortune Labs Prototyping Task List

**Approach:** firmware skeleton first, then breadboard each subsystem, then integrate.
**Principle:** every phase has a testable deliverable. Do not move to the next
phase before the current one has a known-good baseline.

*Last updated: August 2026*

> **This file is the specification and stays that way.** Status is not repeated
> here. A phase that is open, blocked, or closed is open, blocked, or closed in
> the issue tracker: see [`docs/sop/issue_sop.md`](../docs/sop/issue_sop.md).
> A number appearing in both places is a number that will disagree with itself.
>
> The checkboxes below predate the tracker. Where a checkbox and an issue
> disagree, the issue is right.

---

## Phase 0: Setup & Toolchain

- [ ] Install ESP-IDF v5.x (not the Arduino framework; we need full FreeRTOS control)
- [ ] Configure VS Code + the ESP-IDF extension
- [ ] Flash the `hello_world` example, confirm boot on the serial monitor
- [ ] Create the Git repo `fortunelabs-mainboard-fw`, commit at every milestone

**Deliverable:** ESP32-S3 boots, serial output is visible, toolchain verified.

---

## Phase 1: Firmware Skeleton (bare ESP32, no peripherals)

*Goal: get the firmware architecture right before touching any hardware.*

> **"Bare ESP32" is a condition this phase must be testable under, not a
> description of where the code started.** A driver init that aborts on a
> missing I²C device makes every deliverable below unobservable: the image
> never reaches WiFi. Driver init failure therefore degrades, logs, and skips
> the dependent task. It does not stop the system.

### 1.1 FreeRTOS Task Architecture

- [ ] Design the task structure on paper or a whiteboard first:
  - `task_sensor`: periodic ADC read (dummy data for now)
  - `task_network_telemetry`: WiFi connect + MQTT publish
  - `task_actuator`: relay/actuator control via a command queue
  - `task_display`: renders text lines from a queue through the display contract
  - `task_supervisor`: watchdog, health check, error handling
- [ ] Implement every task as a skeleton with dummy data (`xTaskCreate`, `vTaskDelay`)
- [ ] Set task priorities: supervisor > comm > sensor > output
- [ ] Implement inter-task communication: sensor→comm queue, command→output queue

### 1.2 WiFi + MQTT

- [ ] WiFi STA mode: connect to the router, handle reconnect automatically
- [ ] MQTT client: connect to a broker (public HiveMQ or local Mosquitto)
- [ ] Publish dummy sensor data to `fortunelabs/{device_id}/telemetry`
- [ ] Subscribe to `fortunelabs/{device_id}/command` for remote control
- [ ] Test: power the router off and back on, firmware must reconnect without rebooting

### 1.3 System Infrastructure

- [ ] NVS (Non-Volatile Storage): store WiFi SSID/password and device config
- [ ] OTA update mechanism, minimally: download firmware over HTTP, flash, reboot
- [ ] Watchdog timer: task watchdog on every task, panic handler
- [ ] Logging framework: level-based (ERROR/WARN/INFO/DEBUG), output to serial and optionally MQTT
- [ ] Uptime counter + free heap monitoring, published to MQTT as a heartbeat

**Deliverable:** ESP32 connects to WiFi, publishes dummy telemetry to MQTT every
5 seconds, receives commands, survives a WiFi dropout. Every task runs under
FreeRTOS without crashing for 24 hours.

---

## Phase 2: Breadboard, I²C Bus Validation

*Goal: prove the I²C bus works before adding devices to it.*

### 2.1 Wiring

- [ ] 2.2 kΩ I²C pull-up resistors to 3.3V on SDA and SCL
- [ ] Connect the ADS1115 to the I²C bus (default address 0x48, ADDR→GND)
- [ ] Connect the MCP23017 to the I²C bus (default address 0x20, A0/A1/A2→GND)
- [ ] Connect the SSD1306 OLED to the I²C bus (default address 0x3C)
- [ ] 0.1 µF decoupling cap at the VDD of every IC

### 2.2 Firmware

- [ ] I²C master init: GPIO assignment, 400 kHz clock
- [ ] I²C bus scan: detect every device, print the addresses found
- [ ] Verify the ADS1115 appears at 0x48, the MCP23017 at 0x20, the SSD1306 at 0x3C

> **The bus rate is set per device, not once for the bus.** ESP-IDF's I²C
> master driver takes `scl_speed_hz` at device registration, so a single
> bus-level figure does not describe what any transaction actually runs at.
> Every device registration states its own rate, and the rate is recorded with
> any measurement taken on this bus.

**Deliverable:** the serial log shows every device detected at its correct
address, at a stated bus rate. Screenshot or photo as documentation.

---

## Phase 3: Breadboard, ADS1115 Analog Input

*Goal: read an analog sensor, validate accuracy and noise.*

### 3.1 Hardware

- [ ] Connect a potentiometer (or voltage divider) to AIN0 as the test signal
- [ ] Fit a 0.1 µF decoupling cap close to the ADS1115 VDD
- [ ] (Optional) RC anti-alias filter on the input, sized for the chosen data rate

### 3.2 Firmware

- [ ] ADS1115 driver, configured over I²C: PGA gain, data rate, single-shot mode
- [ ] Read a single channel (AIN0) in a loop, print the raw value + voltage conversion
- [ ] Test every relevant PGA range (±4.096V for a 0-3.3V input)
- [ ] Measure the noise floor: 100 samples with the input shorted to GND, compute the standard deviation
- [ ] Integrate into `task_sensor_read`, replacing dummy data with real ADC readings
- [ ] Publish ADC readings to MQTT

### 3.3 Validation

- [ ] Compare ADC readings against a multimeter at 5 points (0V, 0.8V, 1.65V, 2.5V, 3.3V)
- [ ] Is the noise level acceptable? (target: <2 LSB at 16-bit for a DC signal)
- [ ] Does the chosen data rate match what the application needs?

**Deliverable:** real analog data flows ADS1115 → ESP32 → MQTT. Noise
characterization documented.

---

## Phase 4: Breadboard, MCP23017 GPIO Expander

*Goal: extra digital I/O under control, interrupts tested.*

### 4.1 Output Test

- [ ] Configure Port A (GPA0-GPA7) as outputs
- [ ] Toggle an LED on 2-3 pins, verify on/off via a firmware command
- [ ] Test all 8 Port A pins

### 4.2 Input Test

- [ ] Configure Port B (GPB0-GPB7) as inputs with internal pull-ups
- [ ] Connect a push button to 1-2 pins
- [ ] Read the input state, print it to serial
- [ ] Configure the interrupts (INTA/INTB): wire-OR open-drain into an ESP32 GPIO
- [ ] Test interrupt-on-change: press the button → ISR fires on the ESP32 → read the MCP23017 register

### 4.3 Integration

- [ ] MCP23017 output control via MQTT command (subscribe topic → parse → set pin)
- [ ] MCP23017 input state published to MQTT on change (interrupt-driven)

**Deliverable:** remote LED toggle over MQTT. A button press shows up as an MQTT
message. Interrupts work.

---

## Phase 5: Breadboard, ULN2803A + Relay

*Goal: drive a relay from an MCP23017 output through a Darlington driver.*

### 5.1 Hardware

- [ ] Connect MCP23017 GPA outputs to the ULN2803A inputs
- [ ] ULN2803A output to the relay coil (5V relay, common pin to 5V)
- [ ] Clamp diodes are built into the ULN2803A: verify clean relay switching (no bounce issue)
- [ ] Power the relay coil from a separate 5V rail, not from the ESP32 3.3V

### 5.2 Firmware

- [ ] Full chain test: MQTT command → ESP32 → I²C → MCP23017 → ULN2803A → relay click
- [ ] Verify V_CE(sat) under the actual load: measure the voltage drop with a multimeter
- [ ] Verify the 3.3V drive from the MCP23017 clears V_I(on) of the ULN2803A (margin ≥0.3V)

### 5.3 Validation

- [ ] 1000× relay switching with no misses, as a reliability test
- [ ] Measure the per-channel ULN2803A current, verify it stays under 200 mA (safe zone for a 3.3V drive)
- [ ] Thermal check: the ULN2803A does not overheat after an hour of continuous operation

**Deliverable:** end-to-end relay control from the cloud. Measured V_I(on),
V_CE(sat), I_C documented.

---

## Phase 6: Full Breadboard Integration

*Goal: every subsystem running together in one RTOS environment.*

### 6.1 Integration Test

- [ ] Every device on the I²C bus runs simultaneously without bus contention
- [ ] Sensor read + relay control + MQTT publish/subscribe run concurrently
- [ ] Stress test for 24 hours: high publish rate (every 1 second) + relay toggling + ADC reads
- [ ] Memory leak check: free heap stable after 24 hours of operation
- [ ] A WiFi disconnect/reconnect does not disturb I²C operation

### 6.2 Power Measurement

- [ ] Measure total current draw on the 3.3V rail: idle, WiFi Tx, relay active, everything active
- [ ] Record the peak current during a WiFi Tx burst
- [ ] Compute whether the TPS62162 (1A max) is enough for the 3.3V rail. Peak > 800 mA raises a risk flag
- [ ] Measure the 5V rail draw (relay coil) **against whatever is established to source it**. Until `VDD_5V` has an identified source this measurement has no supply to be taken against

### 6.3 Documentation

- [ ] Photo of the final breadboard setup
- [ ] Wiring diagram (hand-drawn or Fritzing is fine)
- [ ] Pin assignment table, ESP32-S3 to every peripheral
- [ ] Log the measurements: ADC accuracy, noise floor, power draw, V_CE(sat)

**Deliverable:** the full system runs 24+ hours without crashing. Power budget
validated. Every measurement documented. Go/no-go decision for the PCB design.

---

## Phase 7: Schematic Capture

*Starts AFTER Phase 6 closes. Breadboard insights go straight into the schematic.*

- [ ] Pick an EDA tool (KiCad 8 recommended: free, open source, 4-layer capable)
- [ ] Schematic: input protection block (AO3401A + Zener + TVS + Polyfuse)
- [ ] Schematic: TPS62162DSG 12V→3.3V, single stage. Supersedes the two-stage
      MP2393→MP2388 rail and its component values, which are not on the board.
      See [`docs/adr/2026-07-29-power-rail-is-tps62162.md`](../docs/adr/2026-07-29-power-rail-is-tps62162.md)
- [ ] Establish what sources `VDD_5V`. No BOM line produces it. If the answer is
      USB-C VBUS, Phase 5's relay supply does not exist in field deployment
- [ ] Schematic: ESP32-S3, every power pin, strapping pins, crystal, RF matching network
- [ ] Schematic: I²C bus, 2× ADS1115 + MCP23017 + 2.2 kΩ pull-ups
- [ ] Schematic: ULN2803A output driver
- [ ] Schematic: DS3232M RTC + CR2032 backup cell
- [ ] Schematic: TPL5010 watchdog timer (`ESP32_WDT_DONE`)
- [ ] Schematic: USB-C receptacle, CC pull-downs, and the SD card interface
- [ ] Schematic: daughter board connector (define the standard pinout)
- [ ] Schematic: decoupling cap placement per IC
- [ ] ERC (Electrical Rule Check): zero errors
- [ ] Peer review of the schematic (or self-review after a 48-hour gap)

**Deliverable:** complete schematic, clean ERC, ready for PCB layout.

---

## Phase 8: PCB Layout & Fabrication

- [ ] 4-layer stackup: Top / GND (L2) / Power (L3) / Bottom
- [ ] 50Ω RF trace impedance: calculator, verified against the vendor stackup
- [ ] Power trace width: ≥25 mil main, ≥20 mil VDD3P3, ≥10 mil elsewhere
- [ ] ESP32-S3 ground pad: minimum 9 thermal vias
- [ ] CLC/LC filter on VDD3P3 pins 2/3 (RF supply)
- [ ] Star-shaped power distribution
- [ ] Clean DRC: zero errors
- [ ] Gerber export + review in a Gerber viewer
- [ ] Order the PCB (JLCPCB/PCBWay), starting at 5 pcs
- [ ] Finalize the BOM + order components (LCSC/Mouser/Digikey)
- [ ] Assembly: evaluate self-soldering against an assembly service for the QFN packages
- [ ] Bench test in the same sequence as the breadboard: power block → MCU boot → I²C → ADC → output

**Deliverable:** working PCB prototype. All bench tests pass.

---

## Phase 9: Ecosystem Integration

- [ ] Daughter board template: standard connector, pinout definition, one simple example board
- [ ] Monitoring dashboard: React + MQTT (Grafana is also an option for a fast MVP)
- [ ] WhatsApp notification: via API (Twilio/Fonnte/direct WhatsApp Business API)
- [ ] Deployment documentation: a setup guide for field technicians
- [ ] Packaging: mainboard + daughter board + firmware + dashboard as one client bundle

**Deliverable:** the full ecosystem is demo-ready. One bundle that can be
deployed to the first client.

---

## Decision Log

*Decisions already taken. Every line here whose argument is still live belongs to
a `type:decision` issue and is closed by an ADR in [`docs/adr/`](../docs/adr/).
See [`docs/sop/git_sop.md`](../docs/sop/git_sop.md).*

| Date | Decision | Reason |
|---|---|---|
| May 2026 | ESP-IDF, not Arduino | Full FreeRTOS control, OTA, NVS. Production-grade. |
| May 2026 | Firmware skeleton first | Avoid refactoring; task architecture determines the hardware interface |
| TBD | ADS1115 data rate | Pick once the sampling requirement of the first use case is known |
| TBD | Driver init: abort vs degrade | Aborting on a missing I²C device makes Phase 1 untestable bare. Degrading changes boot semantics and needs an ADR |
| ~~May 2026~~ | ~~12V→5V: MP2393 (3A)~~ | Superseded 2026-07-29 by the single-stage TPS62162. Neither MPS part is on the board. See the ADR |
| Jul 2026 | 12V→3.3V: TPS62162DSG, single stage | Reason not on record. Reconstructed in [`docs/adr/2026-07-29-power-rail-is-tps62162.md`](../docs/adr/2026-07-29-power-rail-is-tps62162.md), which stays **Proposed** until the rationale is supplied |
| TBD | 2-layer vs 4-layer PCB | Evaluate after the breadboard. 2-layer is cheaper for early iterations |

---

## Risk Tracker

*Every line names the phase that settles it. An `Open` line is a gate whose
prediction is already written down, and the Mitigation column carries the value
and its failure signature, which is exactly what the `gate` template asks for.*

| Risk | Severity | Status | Mitigation |
|---|---|---|---|
| TPS62162 1A too little for peak WiFi Tx + peripherals | Medium | Open, verify Phase 6 | Power budget measurement against 1A on the single 3.3V rail. The retired MP2388 row asked this of a part that is not fitted; the single-stage design relocated the risk rather than removing it |
| `VDD_5V` has no identified source | High | Open, settles Phase 7 | No BOM line produces it. If it is USB-C VBUS only, Phase 5's relay coils have no field supply and the schematic changes before the PCB |
| ULN2803A 3.3V drive sits at the lower bound of V_I(on) | Low | Open, verify Phase 5 | Bench measure. Fallback: TPIC6B595 shift register driver |
| I²C bus capacitance >200pF | Low | Open, verify Phase 2 | Compute trace length. Fallback: drop to 100 kHz |
| Board carries subsystems no phase plans and no firmware drives | Medium | Open, settles Phase 7 | DS3232M, TPL5010, SD card, USB-C and the second ADS1115 are on the schematic, absent from Phases 1-6, and have no driver. Either they enter the phase plan or they are not populated on the first spin |
| Boot aborts without the I²C peripherals fitted | High | Open, settles Phase 1 | `ESP_ERROR_CHECK` on ADS1115 and SSD1306 init panics and reboots on a bare board, so the Phase 1 gates cannot run. Fallback: degrade on init failure and skip the dependent task |
| First hardware project, steep learning curve | High | Active | Fail cheap: breadboard first, PCB later |

---

Each time a phase closes: update the status in the tracker, record the findings,
commit to the repo.
