# Issue SOP

*How GitHub Issues are used on this project. Scope is one developer, so an issue
is never an assignment. It is the only place a prediction can be recorded before
the run that tests it, and the only place a phase's status is allowed to live.*

---

## The model: one deliverable, one issue

[`todos/prototyping_todo.md`](../../todos/prototyping_todo.md) states the rule
this tracker exists to satisfy:

> This file is the specification and stays that way. Status is not repeated here.

Four places, no overlap:

| Question | Answered by |
|---|---|
| What must be true, and how is it tested? | `todos/prototyping_todo.md`, the phase and its **Deliverable** |
| Has it been tested yet, and what was predicted? | the issue |
| What did it measure? | the CI run, or the measurement log named in the issue |
| Why was it built this way? | `docs/adr/` |

An issue never restates a deliverable. It links to the phase and holds what the
task list cannot: when it was run, what was predicted beforehand, and which
commit closed it.

The prototyping principle, *do not move to the next phase before the current one
has a known-good baseline*, is not enforceable by a checkbox, because a checkbox
cannot say whether the baseline was measured or assumed. That is what
`blocked` and the closing evidence below are for.

---

## Where a criterion lives

Most criteria are in the task list. These are the ones that are not, because code
and CI hold them directly:

| Area | The criterion lives in | Enforced by |
|---|---|---|
| Firmware architecture | [`firmware/Readme.md`](../../firmware/Readme.md): layer map, dependency rules | review |
| Firmware behaviour | the Unity suite under `firmware/test/` | `pio test -e native` |
| Firmware build | [`.github/workflows/build.yml`](../../.github/workflows/build.yml) | CI |
| Schematic and PCB | [`.github/workflows/kicad-ci.yml`](../../.github/workflows/kicad-ci.yml): ERC, DRC | CI |
| KiCad in git | [`README-kicad-git-workflow.md`](../../hardware/fortunelabs_mainboard_v0/README-kicad-git-workflow.md) | review |

Everything in Phases 2 through 6 is a bench deliverable: a multimeter reading, a
24-hour run, a photograph. **None of it leaves a trace in git.** An issue is the
only record that a bench deliverable happened at all, which is why a phase is
never closed by the code that implements it.

---

## Identity

**A gate's handle is the name the thing already has.** Where CI runs it, the job
name; where the task list numbers it, that number; otherwise the command.

```
build           the ESP-IDF build job
native-tests    the host-native Unity job
erc             kicad-cli sch erc
4.2 input       the sub-section of the task list
```

**Title format:** `P<phase> <handle>: <the claim, in the task list's own words>`

```
P4 4.2 input: interrupt-on-change fires an ISR on the ESP32
P6 power: the 3.3V rail peak stays inside what MP2388 can supply
```

The phase prefix keeps the search box useful once four phases are open at once.
The area (hardware, firmware, software) is a label rather than part of the
title, because several phases have deliverables in more than one.

---

## Labels

Small on purpose. A tracker with thirty labels is a tracker whose labels are not
read.

| Label | Meaning |
|---|---|
| `type:gate` | has a claim, a command or a measurement, and a criterion that can fail |
| `type:bench` | physical work with no criterion of its own |
| `type:anomaly` | something the task list did not predict |
| `type:decision` | an open argument that will produce an ADR |
| `type:chore` | tooling and repository work |
| `blocked` | an earlier phase is still open |
| `prediction-missing` | opened after its run; the result is worth less |
| `invalidated` | closed once, and something below it changed |
| `phase:0` … `phase:9` | the phase in `todos/prototyping_todo.md` |
| `area:hardware` `area:firmware` `area:software` | which arm of the monorepo |

`phase:` mirrors the task list exactly, so a label is never a second opinion
about which phase something belongs to. `area:` exists because this is a
monorepo: without it a KiCad issue and a driver issue are indistinguishable in
the list view.

`prediction-missing` is a label rather than a note because it must be visible
without opening the issue. A prediction added after the run is worth nothing, and
the tracker should say so at a glance.

There is deliberately **no severity label**. The Risk Tracker already carries
severity, and a severity that lives in two places is a severity that will
disagree with itself.

Create them:

```sh
R=fortunelabs-io/fortunelabs-mainboard-l
gh label create "type:gate"     -R $R -c "0e8a16" -d "claim, command, criterion that can fail"
gh label create "type:bench"    -R $R -c "bfd4f2" -d "physical work, no command of its own"
gh label create "type:anomaly"  -R $R -c "d93f0b" -d "not predicted by the criterion"
gh label create "type:decision" -R $R -c "fbca04" -d "open argument, produces an ADR"
gh label create "type:chore"    -R $R -c "ededed" -d "tooling and repository work"
gh label create "blocked"            -R $R -c "b60205" -d "an open gate sits above this one"
gh label create "prediction-missing" -R $R -c "e99695" -d "opened after its run; worth less"
gh label create "invalidated"        -R $R -c "d93f0b" -d "closed once, something below it changed"
gh label create "area:hardware" -R $R -c "c5def5" -d "KiCad schematic, PCB, BOM, fabrication"
gh label create "area:firmware" -R $R -c "c5def5" -d "the ESP-IDF application on the ESP32-S3"
gh label create "area:software" -R $R -c "c5def5" -d "host-side tooling and services"
for n in 0 1 2 3 4 5 6 7 8 9; do
  gh label create "phase:$n" -R $R -c "5319e7" -d "<phase name from todos/prototyping_todo.md>"
done
```

The nine GitHub defaults (`bug`, `enhancement`, `good first issue`, …) describe a
project that takes contributions. Delete them.

---

## Lifecycle

### 1. Opened before the run

A gate issue is opened before its command is run or its measurement taken, and
the prediction is written into it at that moment. This is the issue's whole
reason to exist. An issue opened afterwards has its prediction field left empty
and carries `prediction-missing`; the prediction is not backdated, because a
backdated prediction is indistinguishable from a correct one and devalues every
other prediction in the tracker by association.

Phase 6 is the clearest case. *"Peak > 800 mA raises a risk flag"* is a prediction with
a number and a failure signature, already written, before the meter is connected.
Recording it in the issue costs nothing and is the difference between a
measurement and a confirmation.

### 2. One phase at a time

Open the issues for a phase when the phase above it closes. Ten open phases
cannot show which one is next, and the ordering in the task list exists because
of the work a late failure invalidates behind it.

Where a phase's deliverable is proved by a later phase, the two issues reference
each other at the time the first is opened.

### 3. Blocked

An issue in a phase whose predecessor is open carries `blocked`. Remove the label
when the predecessor closes.

**Work that runs ahead of its phase is not forbidden, but it is recorded.** When
it happens, the issue says which phase it jumped and what would have to be redone
if that phase later fails. Schematic capture beginning before Phase 6 closes is
exactly this: Phase 6 is where the MP2388 power budget is measured, and the
mitigation for that risk is a different part, which is a schematic change.

### 4. Closed by evidence

A gate issue closes when all three are true, and all three are named in the
issue:

1. The command exited 0, or the measurement was taken, with the CI run cited, or
   the numbers recorded in the issue.
2. Whether the prediction held is written down. **Especially when it did not.**
3. The task list's Deliverable for that phase is satisfied as written, not as
   remembered.

Close it with `Closes #N` in the commit, not by clicking the button. The link
then lives in git history, which survives the repository moving hosts.

A bench issue closes by naming the gate issue that proved it. It never closes on
its own authority. A solder joint is proved by the chain working, not by
inspecting the joint.

### 5. Invalidation reopens; it does not duplicate

When a closed phase's premise changes, whether a driver rewrite, a part
substitution, or a deliverable corrected, **reopen the original issue**, label it `invalidated`,
and write what changed. Do not open a fresh one. A gate that passed, stopped
passing, and passed again is the most informative object in the tracker, and
splitting it across three issue numbers throws that away.

### 6. Anomalies

Anything the task list did not predict gets a `type:anomaly` issue, referencing
the phase it appeared under. It closes by a commit that folds it into the task
list, an ADR that decides it, or a written statement that it is out of scope with
the reason. An anomaly is never closed silently.

### 7. Decisions become ADRs

A `type:decision` issue is where the argument happens and the alternatives are
written down while they are still live. It closes when the ADR commit lands, with
the ADR path in the closing comment. Written afterwards, the alternatives are
reconstructed, and reconstructed alternatives are always the ones that were easy
to reject.

**Every `TBD` row in the Decision Log is an open `type:decision` issue.** A `TBD`
in a table is a decision nobody is arguing about; an issue is one somebody is.

### 8. Risks are gates with the prediction already written

Every row of the Risk Tracker names the phase that resolves it and the fallback
if it fails. That is a claim, a criterion, and a failure signature: the three
things the `gate` template asks for. A risk row becomes a gate issue when its
phase opens, and the row's Status column is then maintained by closing the issue,
not by editing the table.

---

## Milestones

One milestone per phase, named `Phase 0` … `Phase 9`, matching the task list
headings. A milestone closes when every issue in it is closed and the phase's
Deliverable is satisfied. The progress bar is then a real statement about the
project, which is unusual for a progress bar and worth not spoiling with issues
that do not gate anything.

---

## The project board

One project per phase, named with the phase's own heading from the task list.
Currently
[`Phase 4: Breadboard, MCP23017 GPIO Expander`](https://github.com/orgs/fortunelabs-io/projects/4),
linked to the repository so it appears under
[its Projects tab](https://github.com/fortunelabs-io/fortunelabs-mainboard-l/projects).

| Column | An issue is here when |
|---|---|
| Todo | it is open and not `blocked` |
| In Progress | its command is running, or the bench work is under way |
| Done | it is closed, by a commit, with its evidence cited |

**An issue joins the board when it stops being `blocked`, not when it is
opened.** A board holding every deliverable of every phase shows the same thing
the issue list already shows, and it is the one view that could have answered
*what is next*.

The board is a view, never a source. Nothing is recorded there that is not
already in the issue. A note dragged onto a card will not be found again from
git history.

---

## What does not get an issue

Anything that will be done within the hour. Anything already fully stated in the
task list and not yet reached; that is what the task list is for. Anything whose
only content is "remember to". A tracker holding today's shell commands is a
tracker nobody opens, and once it is not opened, the gate issues in it stop being
a record of anything.
