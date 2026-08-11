# IC drivers keep an internal vtable

Status: Accepted
Date: 2026-07-21
Issue: none. The argument was held in `changelog_2026_07_21.md`, since removed
from the repository root. This record is written from that changelog and from the
diff of `67a5010`, not from an open `type:decision` issue. Future decisions of
this weight get the issue first.

## Context

Two different things in this firmware are called a vtable, and they are not the
same thing.

The first is the **HAL contract**: `sensor_driver_t`, `output_driver_t`,
`display_driver_t`, `transport_driver_t`. The indirection there is load-bearing:
the task layer depends on the contract, the implementation behind it is meant to
vary, and `main.c` picks which one is wired in. Nothing in this record touches
that.

The second is the **IC driver's own vtable**: `ads1115_driver_t` reached through
`ads1115_get_driver()`, and the same shape in MCP23017. Here the part is fixed in
the BOM and there is no substitution case, so the indirection buys no
polymorphism. It exists only as a way of presenting the driver's surface.

The constitution at the time (`firmware/gold_standard.md` Decision #1, removed
from the repository in `67a5010`; copies survive outside it) mandated removing
that second vtable: plain public functions plus `static` helpers, on the grounds
that MCP23017 already proved a single access path and hidden internals are
achieved by `static` alone.

On 2026-07-21 the mandate was executed in full, with both drivers de-vtabled and the
Unity suite updated to call the flat functions directly. `idf.py build` passed
and all 59 native test cases passed. Later the same day it was reverted.

## Alternatives

### Flat public functions plus static helpers (the mandated pattern)

**For it.** No indirection at all. Encapsulation is already delivered by
`static`, so the vtable adds nothing the language did not. One fewer concept for
a reader to hold. Call sites name the function actually being called, so grep and
jump-to-definition land on the real definition instead of on a struct member. No
`get_driver()` ceremony before use.

**Against it.** The driver's surface stops being a named, grouped thing; nothing
in the header says *these four operations together are the driver*. On review the
flat drivers read worse day to day. That cost is paid on every reading of the
code, where the indirection it removes is paid once at the CPU, in a pointer
dereference already dwarfed by the I²C transaction it precedes.

### Internal vtable reached through `*_get_driver()` (kept)

**For it.** The driver's operations are one named surface a reader can see whole.
It is symmetric with the HAL contracts one layer up, so the codebase has one
shape rather than two shapes that must be told apart. Call sites read uniformly
whichever layer they are at.

**Against it.** It is an indirection that buys no polymorphism, in a place where
the part cannot be substituted. It costs an accessor call before use, and it
introduces a second vtable *level* whose meaning differs from the first, so a
reader who has not been told will reasonably assume the IC vtable is a
substitution boundary too. This record is the mitigation for that.

## Decision

IC drivers keep the internal vtable. `ads1115_driver_t` + `ads1115_get_driver()`
and `mcp23017_driver_t` + `mcp23017_get_driver()` are the house style for
fixed-BOM ICs, and new IC drivers follow it.

Readability in daily use outweighs removing an indirection whose runtime cost is
a pointer dereference.

Two improvements from the reverted work were kept, and are not part of what was
reverted:

- The ADS1115 clock comes from `config->scl_hz` instead of a hardcoded 100 kHz.
- Internal static helpers keep leading-underscore names.

Three changes made in the same commit were never in scope for the revert and
still stand: `task_display` consuming an injected `display_driver_t`,
`network_manager` wrapped behind a `transport_driver_t` adapter, and command
interpretation moved up into the orchestration layer.

## Consequences

**To reverse this**, re-run the de-vtable refactor across both drivers and the
Unity suite. It was done once inside a single day, so the cost is known and
modest, and it is discovered at compile time rather than on the bench.

**The prior constitution now contradicts the code.** Any copy of
`gold_standard.md` / `GOLD_STANDARD.md` dated before 2026-07-21 states the
opposite of this record; for this repository, this record supersedes it. A
finding of the form *"ADS1115 uses an internal vtable, non-conforming"* (F-DRV-01
in the surviving findings document) is closed by this decision, not by a fix.

**The two vtable levels mean different things**, and that has to survive in the
documentation rather than in anyone's memory: the HAL vtable is a substitution
boundary, the IC vtable is a presentation choice. A reader who conflates them
will either add an unnecessary adapter or delete a necessary one.
