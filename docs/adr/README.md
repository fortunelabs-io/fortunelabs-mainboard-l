# Architecture decision records

One file per decision, `YYYY-MM-DD-<kebab-slug>.md`, the date being the date of
the decision rather than of the writing.

```markdown
# <the decision, as a statement>

Status: Proposed | Accepted | Superseded by <file>
Date: YYYY-MM-DD
Issue: #N

## Context

What forced a decision, and what was already fixed by the board or an earlier
record.

## Alternatives

Each with the reason it would be taken and the reason it would not. This is the
payload; a record listing only what was chosen has recorded nothing.

## Decision

## Consequences

What has to be redone if this is reversed, and when that would be discovered.
```

The Decision section of an Accepted record is never edited. Supersede it with a
new file naming what survives and what changes. See
[`../sop/git_sop.md`](../sop/git_sop.md).

## Records

- [`2026-07-21-ic-driver-inner-vtable.md`](./2026-07-21-ic-driver-inner-vtable.md)
  keeps IC drivers on an internal vtable. Supersedes the removed
  `firmware/gold_standard.md` Decision #1, which mandated the opposite.
- [`2026-07-29-power-rail-is-tps62162.md`](./2026-07-29-power-rail-is-tps62162.md)
  records the single-stage TPS62162 3.3 V rail that replaced the two-stage
  MP2393→MP2388 pair, on sourcing rather than technical grounds, and states
  that the board has no 5 V rail and needs none.

## Owed

One decision this repository has taken and not recorded: **the I²C clock.** The
task list specifies 400 kHz, the bus config in `main.c` sets 100 kHz, SSD1306
registers at 400 kHz, and the Risk Tracker names the drop to 100 kHz as the
fallback for bus capacitance. The bus-level figure is inert, since the clock
is applied per device, so the code cannot say what the clock is, or whether the fallback
was deliberate.

The `esp32` `sdkconfig` on an ESP32-S3 board is not an owed record. Nobody chose
it; it is stale from the `hello_world` template. It is a deviation, listed as one
in [`firmware/Readme.md`](../../firmware/Readme.md), and it closes as a
`type:anomaly` issue. See [`../sop/git_sop.md`](../sop/git_sop.md).
