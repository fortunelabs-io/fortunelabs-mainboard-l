# The 3.3 V rail is a single TPS62162, and the board has no 5 V rail

**Date:** 2026-07-29 **Status:** Accepted
**Supersedes:** the Decision Log row *"12V→5V: MP2393 (3A)"* in
`todos/prototyping_todo.md`, and the Phase 7 schematic lines that name MP2393
and MP2388 with their component values.

## Context

`todos/prototyping_todo.md` specifies a two-stage MPS rail, in the Decision Log
(May 2026, decision *"12V→5V: MP2393 (3A)"*, reason *"MPS family, 3A headroom
for relay + stage 2, PG output for power sequencing, COT control"*) and again in
Phase 7, down to component values: MP2393 with R1=40.2 kΩ, R2=7.68 kΩ,
RT=15 kΩ, L=4.9 µH, C_BST=1 µF / R_BST=20 Ω, C_SS=6.8 nF, EN=604 kΩ to VIN,
PG→ESP32 GPIO; then MP2388 5V→3.3V with R1=75 kΩ, R2=24 kΩ, AAM R3=80.6 kΩ.

The schematic does not contain that design, and has not since
`767e999 feat: adding power management module` (2026-07-29), which carried no
reasoning in its message and left the specification behind.

Verified against the working tree at `hardware/fortunelabs_mainboard_v0/`, from
a `kicad-cli sch export netlist` rather than by reading the sheet:

- **No MPS part appears anywhere in the schematic.** A search for `MP2393`,
  `MP2388` and `MP2315` returns zero hits.
- **U5 is a TPS62162DSG** (TI, WSON-8), the only switching regulator on the
  board. `U5.2 (VIN)` and `U5.3 (EN)` sit on `+12V`; `U5.7 (SW)` drives L1;
  `U5.6 (VOS)` sits on `+3V3`.
- **`VOS` tied to the output is a fixed-output part sensing itself.** There is
  no feedback divider, so the output voltage is not a value anyone set.
- **`U5.8 (PG)` drives `ESP32_PG`.** The power-sequencing capability that
  motivated the original MPS choice survived the part change.
- `+3V3` carries 39 nodes and supplies every IC: U1 and U4 (ADS1115), U2
  (MCP23017), U3.2 (ESP32-S3), U6 (DS3232M), U7 (TPL5010), J4 (SD card).

## Alternatives considered

- **The two-stage MPS pair as specified (MP2393 → MP2388).** 12 V to 5 V at 3 A,
  then 5 V to 3.3 V, with a PG output for sequencing. **Rejected on stock.** The
  parts could not be sourced. This is a sourcing fact and not a technical one:
  no measurement or datasheet comparison found the MPS design wanting, and this
  record does not claim one did.
- **Another MPS part 12 V to 3.3 V (MP2315 or similar).** Would have kept the
  vendor family the original decision named. Same sourcing problem.
- **A single TPS62162, 12 V directly to 3.3 V.** Chosen. Available, and
  collapsing two stages into one removes an inductor, a feedback network and
  roughly twenty passives from a first-ever PCB layout. It also retires the
  MP2388 headroom question by deleting the stage that raised it.

## Decision

The 3.3 V rail is produced by a single fixed-output TPS62162DSG (U5) from
`+12V`. The MP2393 and MP2388 are not fitted and the two-stage architecture is
abandoned. `PG` remains wired to the ESP32.

**The board has no 5 V rail, and nothing on it needs one.** Every IC runs from
`+3V3`. The ULN2803A (U8) has no supply pin at all: it is an open-collector
Darlington array with `U8.9` to GND and `U8.10 (COM)` brought out to J5, so the
relay coil supply and its flyback clamp come from the relay board through J5,
which carries `+12V`. USB-C VBUS (J3.A4/A9/B4/B9) is `no_connect`, so USB
supplies no power either.

## What this decision does not claim

It does not claim the TPS62162 is technically superior to the MPS pair. It was
available and they were not. Recording it as a sourcing decision keeps it from
being read later as a considered comparison that never happened.

It does not claim the 1 A limit has been shown adequate. The retired Risk
Tracker row *"MP2388 1A too little for peak WiFi Tx + peripherals"* asked a real
question of a part that is not fitted. The single-stage design **relocated** that
question to the TPS62162 rather than answering it, and it stays open against
Phase 6.

## Consequences

- **`VDD_5V` is vestigial.** It survives only as a local label on two
  daughterboard connector pins, left behind when the 5 V stage was removed. It
  is deleted, not sourced.
- **Phase 5 changes supply.** It is specified against a 5 V relay coil from a
  separate 5 V rail. There is no such rail and there is no plan to add one:
  relays run from `+12V` through J5.
- **Three daughterboard power nets are electrically dead**, and this record is
  how they were found. `/VDD_3V3` (J1.23, J2.25), `/VDD_5V` (J1.25, J2.27) and
  `/VIN_12V` (J1.27, J2.29) each connect two connector pins to each other and to
  nothing else. The local labels `VDD_*` were never tied to the `+3V3` and
  `+12V` power symbols that carry the real rails. J1.21 is on real `+3V3` and
  J5.13/14 on real `+12V`, so the connectors have one working power pin and
  three dead ones. `VDD_5V` is deleted; the other two are wiring defects and are
  owed an issue before layout.
- **ERC cannot see any of this.** `kicad-cli sch erc` reports zero violations
  today: two passive connector pins on a net is legal. Gate #26 is closeable now
  and would prove nothing about these nets, which is worth stating in that issue
  before it is run.
- **Phase 6's power budget is one measurement, not two.** It was written as a
  3.3 V rail against 1 A and a 5 V rail against 3 A.

## What would reopen this

The Phase 6 measurement showing peak draw on the 3.3 V rail within a working
margin of the TPS62162's 1 A. That is the same failure the retired MP2388 row
predicted, moved to a different part; the single-stage design did not remove the
risk, it relocated it.
