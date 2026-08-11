# Fortune Labs — Prototyping Task List

**Pendekatan:** Firmware skeleton first → breadboard per subsistem → integrasi
**Prinsip:** Setiap phase punya deliverable testable. Jangan lanjut ke phase
berikutnya sebelum phase sekarang punya known-good baseline.

*Last updated: Mei 2026*

> **This file is the specification and stays that way.** Status is not repeated
> here. A phase that is open, blocked, or closed is open, blocked, or closed in
> the issue tracker — see [`docs/sop/issue_sop.md`](../docs/sop/issue_sop.md).
> A number appearing in both places is a number that will disagree with itself.
>
> The checkboxes below predate the tracker. Where a checkbox and an issue
> disagree, the issue is right.

---

## Phase 0 — Setup & Toolchain

- [ ] Install ESP-IDF v5.x (bukan Arduino framework — kita butuh kontrol penuh FreeRTOS)
- [ ] Konfigurasi VS Code + ESP-IDF extension
- [ ] Flash "hello_world" example → serial monitor konfirmasi boot OK
- [ ] Buat Git repo: `fortunelabs-mainboard-fw` — commit setiap milestone

**Deliverable:** ESP32-S3 boots, serial output terlihat, toolchain verified.

---

## Phase 1 — Firmware Skeleton (ESP32 Naked, Tanpa Peripheral)

*Tujuan: arsitektur firmware yang benar sebelum menyentuh hardware apapun.*

### 1.1 FreeRTOS Task Architecture

- [ ] Desain task structure di kertas/whiteboard dulu:
  - `task_sensor_read` — baca ADC periodik (nanti, sekarang dummy data)
  - `task_comm` — WiFi connect + MQTT publish
  - `task_output_ctrl` — kontrol relay/aktuator via queue command
  - `task_supervisor` — watchdog, health check, error handling
- [ ] Implementasi skeleton semua task dengan dummy data (`xTaskCreate`, `vTaskDelay`)
- [ ] Tentukan prioritas task: supervisor > comm > sensor > output
- [ ] Implementasi inter-task communication: queue antara sensor→comm, queue command→output

### 1.2 WiFi + MQTT

- [ ] WiFi STA mode — connect ke router, handle reconnect otomatis
- [ ] MQTT client — connect ke broker (HiveMQ public atau Mosquitto lokal)
- [ ] Publish dummy sensor data ke topic `fortunelabs/{device_id}/telemetry`
- [ ] Subscribe ke topic `fortunelabs/{device_id}/command` untuk remote control
- [ ] Test: matikan WiFi router, nyalakan lagi — firmware harus reconnect tanpa reboot

### 1.3 System Infrastructure

- [ ] NVS (Non-Volatile Storage) — simpan WiFi SSID/password, device config
- [ ] OTA update mechanism — minimal: download firmware via HTTP, flash, reboot
- [ ] Watchdog timer — task watchdog pada setiap task, panic handler
- [ ] Logging framework — level-based (ERROR/WARN/INFO/DEBUG), output ke serial + optional MQTT
- [ ] Uptime counter + free heap monitoring → publish ke MQTT sebagai heartbeat

**Deliverable:** ESP32 connect WiFi, publish dummy telemetry ke MQTT setiap 5
detik, terima command, survive WiFi dropout. Semua task jalan di FreeRTOS tanpa
crash 24 jam.

---

## Phase 2 — Breadboard: I²C Bus Validation

*Tujuan: pastikan I²C bus berfungsi sebelum menambahkan device.*

### 2.1 Wiring

- [ ] I²C pull-up resistor 2.2kΩ ke 3.3V pada SDA dan SCL
- [ ] Hubungkan ADS1115 ke I²C bus (alamat default 0x48, ADDR→GND)
- [ ] Hubungkan MCP23017 ke I²C bus (alamat default 0x20, A0/A1/A2→GND)
- [ ] Decoupling cap 0.1µF di VDD setiap IC

### 2.2 Firmware

- [ ] I²C master init — GPIO assignment, clock 400kHz
- [ ] I²C bus scan — detect semua device, print address yang ditemukan
- [ ] Verify: ADS1115 muncul di 0x48, MCP23017 di 0x20

**Deliverable:** Serial log menunjukkan kedua device terdeteksi di alamat yang
benar. Screenshot/foto sebagai dokumentasi.

---

## Phase 3 — Breadboard: ADS1115 Analog Input

*Tujuan: baca sensor analog, validasi akurasi dan noise.*

### 3.1 Hardware

- [ ] Hubungkan potentiometer (atau voltage divider) ke AIN0 sebagai test signal
- [ ] Pasang decoupling cap 0.1µF dekat VDD ADS1115
- [ ] (Optional) RC anti-alias filter di input — hitung nilai berdasarkan data rate yang dipilih

### 3.2 Firmware

- [ ] ADS1115 driver — konfigurasi via I²C: PGA gain, data rate, single-shot mode
- [ ] Baca single channel (AIN0) dalam loop, print raw value + voltage conversion
- [ ] Test semua PGA range yang relevan (±4.096V untuk 0-3.3V input)
- [ ] Ukur noise floor: baca 100 sampel dengan input di-short ke GND, hitung std deviation
- [ ] Integrasikan ke `task_sensor_read` — gantikan dummy data dengan real ADC reading
- [ ] Publish ADC reading ke MQTT

### 3.3 Validasi

- [ ] Bandingkan ADC reading dengan multimeter pada 5 titik (0V, 0.8V, 1.65V, 2.5V, 3.3V)
- [ ] Noise level acceptable? (target: <2 LSB di 16-bit untuk DC signal)
- [ ] Data rate yang dipilih sesuai kebutuhan aplikasi?

**Deliverable:** Real analog data mengalir dari ADS1115 → ESP32 → MQTT. Noise
characterization documented.

---

## Phase 4 — Breadboard: MCP23017 GPIO Expander

*Tujuan: kontrol digital I/O tambahan, test interrupt.*

### 4.1 Output Test

- [ ] Konfigurasi Port A (GPA0-GPA7) sebagai output
- [ ] Toggle LED pada 2-3 pin — verify on/off via firmware command
- [ ] Test semua 8 pin Port A

### 4.2 Input Test

- [ ] Konfigurasi Port B (GPB0-GPB7) sebagai input dengan internal pull-up
- [ ] Hubungkan push button ke 1-2 pin
- [ ] Baca status input, print ke serial
- [ ] Konfigurasi interrupt (INTA/INTB) — Wire-OR open-drain ke ESP32 GPIO
- [ ] Test interrupt-on-change: tekan button → ISR di ESP32 fire → baca MCP23017 register

### 4.3 Integrasi

- [ ] MCP23017 output control via MQTT command (subscribe topic → parse → set pin)
- [ ] MCP23017 input status publish ke MQTT saat berubah (interrupt-driven)

**Deliverable:** Remote toggle LED via MQTT. Button press muncul sebagai MQTT
message. Interrupt berfungsi.

---

## Phase 5 — Breadboard: ULN2803A + Relay

*Tujuan: drive relay dari MCP23017 output via Darlington driver.*

### 5.1 Hardware

- [ ] Hubungkan MCP23017 GPA output → ULN2803A input
- [ ] ULN2803A output → relay coil (5V relay, common pin ke 5V)
- [ ] Clamp diode sudah built-in di ULN2803A — verify relay switching bersih (no bounce issue)
- [ ] Power relay coil dari 5V rail terpisah (bukan dari 3.3V ESP32)

### 5.2 Firmware

- [ ] Full chain test: MQTT command → ESP32 → I²C → MCP23017 → ULN2803A → relay click
- [ ] Verify V_CE(sat) di beban aktual — ukur tegangan drop dengan multimeter
- [ ] Verify 3.3V drive dari MCP23017 cukup untuk V_I(on) ULN2803A (margin ≥0.3V)

### 5.3 Validasi

- [ ] Relay switching 1000× tanpa miss — reliability test
- [ ] Ukur arus per channel ULN2803A — verify di bawah 200mA (safe zone untuk 3.3V drive)
- [ ] Thermal check: ULN2803A tidak panas berlebihan setelah continuous operation 1 jam

**Deliverable:** End-to-end relay control via cloud. Measured V_I(on),
V_CE(sat), I_C documented.

---

## Phase 6 — Full Breadboard Integration

*Tujuan: semua subsistem jalan bersamaan dalam satu RTOS environment.*

### 6.1 Integration Test

- [ ] Semua device di I²C bus berjalan simultan tanpa bus contention
- [ ] Sensor read + relay control + MQTT publish/subscribe — concurrent operation
- [ ] Stress test: publish rate tinggi (setiap 1 detik) + relay toggling + ADC reading — 24 jam
- [ ] Memory leak check: free heap stabil setelah 24 jam operasi
- [ ] WiFi disconnect/reconnect tidak mengganggu I²C operation

### 6.2 Power Measurement

- [ ] Ukur total current draw di 3.3V rail: idle, WiFi Tx, relay active, semua aktif
- [ ] Catat peak current saat WiFi Tx burst
- [ ] Hitung: apakah MP2388 (1A max) cukup untuk 3.3V rail? Peak > 800mA → flag risiko
- [ ] Ukur total current draw di 5V rail (relay coil + MP2388 input)
- [ ] Hitung: MP2393 (3A max) untuk total 5V load — should be fine, verify anyway

### 6.3 Dokumentasi

- [ ] Foto breadboard setup final
- [ ] Wiring diagram (bisa hand-drawn atau Fritzing)
- [ ] Tabel pin assignment ESP32-S3 → semua peripheral
- [ ] Log hasil pengukuran: ADC accuracy, noise floor, power draw, V_CE(sat)

**Deliverable:** Full system berjalan 24+ jam tanpa crash. Power budget
validated. Semua measurement documented. Go/no-go decision untuk PCB design.

---

## Phase 7 — Schematic Capture

*Dimulai SETELAH Phase 6 selesai. Breadboard insights langsung masuk ke skematik.*

- [ ] Pilih EDA tool (KiCad 8 recommended — gratis, open source, 4-layer capable)
- [ ] Skematik: input protection block (AO3401A + Zener + TVS + Polyfuse)
- [ ] Skematik: MP2393 12V→5V/3A (R1=40.2kΩ, R2=7.68kΩ, RT=15kΩ, L=4.9µH, C_BST=1µF/R_BST=20Ω, C_SS=6.8nF, EN=604kΩ to VIN, PG→ESP32 GPIO for power sequencing)
- [ ] Skematik: MP2388 #2 5V→3.3V (R1=75kΩ, R2=24kΩ, AAM R3=80.6kΩ, layout per datasheet typical app)
- [ ] Skematik: ESP32-S3 — semua power pin, strapping pins, crystal, RF matching network
- [ ] Skematik: I²C bus — ADS1115 + MCP23017 + pull-ups 2.2kΩ
- [ ] Skematik: ULN2803A output driver
- [ ] Skematik: daughter board connector (definisikan pinout standar)
- [ ] Skematik: decoupling caps placement per IC
- [ ] ERC (Electrical Rule Check) — zero errors
- [ ] Peer review skematik (atau self-review setelah 48 jam jeda)

**Deliverable:** Skematik lengkap, ERC clean, siap untuk PCB layout.

---

## Phase 8 — PCB Layout & Fabrication

- [ ] PCB layout 4-layer stackup: Top / GND (L2) / Power (L3) / Bottom
- [ ] RF trace 50Ω impedance — kalkulator, verify dengan stackup vendor
- [ ] Power trace width: ≥25mil main, ≥20mil VDD3P3, ≥10mil lainnya
- [ ] Ground pad ESP32-S3 → min 9 thermal vias
- [ ] CLC/LC filter di VDD3P3 pin 2/3 (RF supply)
- [ ] Star-shaped power distribution
- [ ] DRC clean — zero errors
- [ ] Gerber export + review di gerber viewer
- [ ] Order PCB (JLCPCB/PCBWay) — mulai dari 5 pcs
- [ ] BOM finalisasi + order komponen (LCSC/Mouser/Digikey)
- [ ] Assembly: evaluasi self-solder vs assembly service untuk QFN packages
- [ ] Bench test: power block → MCU boot → I²C → ADC → output — same sequence as breadboard

**Deliverable:** Working PCB prototype. All bench tests pass.

---

## Phase 9 — Ecosystem Integration

- [ ] Daughter board template: konektor standar, pinout definition, contoh daughter board sederhana
- [ ] Monitoring dashboard: React + MQTT (Grafana juga opsi untuk MVP cepat)
- [ ] WhatsApp notification: via API (Twilio/Fonnte/direct WhatsApp Business API)
- [ ] Deployment documentation: setup guide untuk teknisi lapangan
- [ ] Packaging: mainboard + daughter board + firmware + dashboard = satu paket klien

**Deliverable:** Full ecosystem demo-ready. Satu paket yang bisa di-deploy ke
klien pertama.

---

## Decision Log

*Keputusan yang sudah diambil. Setiap baris di sini yang masih hidup argumennya
milik sebuah issue `type:decision`, dan ditutup oleh sebuah ADR di
[`docs/adr/`](../docs/adr/) — lihat [`docs/sop/git_sop.md`](../docs/sop/git_sop.md).*

| Tanggal | Keputusan | Alasan |
|---|---|---|
| Mei 2026 | ESP-IDF, bukan Arduino | Kontrol penuh FreeRTOS, OTA, NVS. Production-grade. |
| Mei 2026 | Firmware skeleton dulu | Avoid refactoring; task architecture determines hardware interface |
| TBD | Data rate ADS1115 | Pilih setelah tahu sampling requirement dari use case pertama |
| Mei 2026 | 12V→5V: MP2393 (3A) | MPS family, 3A headroom untuk relay+stage2, PG output untuk power sequencing, COT control |
| TBD | 2-layer vs 4-layer PCB | Evaluate setelah breadboard — 2-layer lebih murah untuk iterasi awal |

---

## Risiko Tracker

*Setiap baris menyebut phase yang menyelesaikannya. Baris `Open` adalah gate
yang prediksinya sudah tertulis — kolom Mitigasi berisi nilai dan failure
signature-nya, yang persis diminta oleh template `gate`.*

| Risiko | Severity | Status | Mitigasi |
|---|---|---|---|
| MP2388 1A kurang untuk peak WiFi Tx + peripherals | Medium | Open — verify Phase 6 | Power budget measurement. Fallback: MP2315 (3A) |
| ULN2803A 3.3V drive di batas bawah V_I(on) | Low | Open — verify Phase 5 | Bench measure. Fallback: TPIC6B595 shift register driver |
| I²C bus capacitance >200pF | Low | Open — verify Phase 2 | Hitung trace length. Fallback: turunkan ke 100kHz |
| MP2393 3A cukup untuk 5V rail (relay + MP2388 input) | Low | Open — verify Phase 6 | 3A headroom besar vs ~1.1A estimated load |
| First HW project — learning curve tinggi | High | Active | Fail cheap: breadboard dulu, PCB belakangan |

---

Setiap phase selesai → update status di tracker, catat findings, commit ke repo.
