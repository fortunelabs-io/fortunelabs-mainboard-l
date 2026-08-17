# The 3.3 V rail is a single TPS62162, not the two-stage MPS pair

**Date:** 2026-07-29 **Status:** Proposed
**Supersedes:** the Decision Log row *"12V→5V: MP2393 (3A)"* in
`todos/prototyping_todo.md`, and the Phase 7 schematic lines that name MP2393
and MP2388 with their component values.

> **This record is Proposed, not Accepted, and cannot be Accepted as written.**
> The decision is reconstructed from the schematic and the BOM after the fact.
> What was chosen is verifiable; *why* is not recorded anywhere in this
> repository, and the Alternatives section below is therefore reasoning that
> was reconstructed rather than reasoning that was used. Per
> [`README.md`](./README.md), alternatives are the payload of a record, and a
> reconstructed payload is not the payload. The person who took this decision
> supplies the rationale, or this record stays Proposed.

## Context

`todos/prototyping_todo.md` specifies a two-stage MPS rail, in the Decision Log
(May 2026, decision *"12V→5V: MP2393 (3A)"*, reason *"MPS family, 3A headroom
for relay + stage 2, PG output for power sequencing, COT control"*) and again
in Phase 7, down to
component values: MP2393 with R1=40.2 kΩ, R2=7.68 kΩ, RT=15 kΩ, L=4.9 µH,
C_BST=1 µF / R_BST=20 Ω, C_SS=6.8 nF, EN=604 kΩ to VIN, PG→ESP32 GPIO; then
MP2388 5V→3.3V with R1=75 kΩ, R2=24 kΩ, AAM R3=80.6 kΩ.

The schematic does not contain that design. Verified against the working tree at
`hardware/fortunelabs_mainboard_v0/`:

- **No MPS part appears anywhere in the schematic.** A search for `MP2393`,
  `MP2388` and `MP2315` returns zero hits.
- **U5 is a TPS62162DSG** (TI, WSON-8), the only switching regulator on the
  board.
- **L1 is 2.2 µH**, not the 4.9 µH the MP2393 line specifies.
- Three rails are named: `VIN_12V`, `VDD_5V`, `VDD_3V3`.
- `ESP32_PG` survives as a net label, so the power-sequencing intent that
  motivated the MPS choice outlived the part that provided it.

The change landed in `767e999 feat: adding power management module`
(2026-07-29), carrying no reasoning in its message, and the specification was
never updated to follow. The result is that Phase 6's power budget and two Risk
Tracker rows are currently written against parts that are not on the board.

## Alternatives considered

*Reconstructed. See the note above.*

- **The two-stage MPS pair as specified (MP2393 → MP2388).** 12 V to 5 V at 3 A,
  then 5 V to 3.3 V. Gives a genuine 5 V rail at current, which the relay coils
  in Phase 5 are specified to draw from, and a PG output for sequencing. Two
  switching regulators, two inductors, two feedback networks, and roughly twenty
  passives to place and verify on a first-ever PCB layout. The Risk Tracker
  already carried a row doubting whether the second stage's 1 A was enough.
- **A single TPS62162, 12 V directly to 3.3 V.** One regulator, one inductor,
  one feedback network. Removes the intermediate 5 V stage and with it the
  MP2388 headroom risk entirely, rather than measuring it. The cost is that a
  5 V rail, if still required, must come from somewhere else: the schematic's
  `VDD_5V` net appears at only two points and this record does not establish
  what sources it. **Open item, see below.**
- **A single MPS part (MP2315 or similar) 12 V to 3.3 V.** Would have kept the
  vendor family the original decision named, with the same single-stage
  simplification. Whether this was considered is unknown.

## Decision

The 3.3 V rail is produced by a single TPS62162DSG (U5) from `VIN_12V`. The
MP2393 and MP2388 are not fitted and the two-stage architecture is abandoned.

**The reason is not on record.** This section states what the board does. It
does not state why, because that is not recoverable from the artifacts.

## What this decision does not claim

It does not claim the TPS62162 was chosen on technical merit over the MPS pair,
because no comparison is recorded. It does not claim the MPS design was found
inadequate; the Risk Tracker's doubt about MP2388 headroom was never measured,
so it was not resolved, it was made moot. It does not claim the part selection
is final: two open items below have to close before this record can be
Accepted, and either could change the part.

## Open items blocking Accepted status

1. **The rationale.** Supplied by the person who took the decision. Without it
   this record documents a change, not a decision.
2. **The output variant and feedback network.** R22 is 14K3, while R21 and R23
   carry the placeholder value `R` with no resistance assigned. A populated
   feedback divider implies an adjustable part; a fixed-output TPS62162 would
   not need one. Which variant U5 is, and what the divider is for, must be read
   off the datasheet and the schematic rather than inferred here.
3. **What sources `VDD_5V`.** No part in the BOM is identified as producing it.
   If the answer is USB-C VBUS (J3), then the 5 V rail exists only while USB is
   attached, and Phase 5's relay coils, specified to run from a separate 5 V
   rail, have no supply in field deployment. This is the item most likely to
   force a change.

## Consequences

- **Phase 7's power lines and the Decision Log row are wrong as written** and
  are corrected in the same commit as this record.
- **Two Risk Tracker rows become unfalsifiable**: *"MP2388 1A too little for
  peak WiFi Tx + peripherals"* and *"MP2393 3A enough for the 5V rail"*. Neither
  part exists. They are replaced, not ticked. The underlying question, whether
  the 3.3 V rail carries peak WiFi Tx plus peripherals, is still open, now
  against a 1 A TPS62162.
- **Phase 6's power budget changes shape.** It was specified as two
  measurements, a 3.3 V rail and a 5 V rail, with headroom computed against 1 A
  and 3 A. Against a single-stage rail it is one measurement against 1 A, plus
  whatever open item 3 turns out to require.
- **If open item 3 resolves to "USB VBUS only"**, Phase 5 cannot run as written
  and the relay supply becomes a schematic change before the PCB, not after.

## What would reopen this

The Phase 6 measurement showing peak draw on the 3.3 V rail within a working
margin of the TPS62162's 1 A. That is the same failure the retired MP2388 row
predicted, moved to a different part; the single-stage design did not remove the
risk, it relocated it.
