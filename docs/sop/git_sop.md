# Git SOP

*Standard operating procedure for the repository. Scope is one developer. Rules
that exist to coordinate people are absent; every rule below exists because this
repository is the record of a board, the firmware on it, and why both are shaped
the way they are.*

**The question this SOP answers.** *Which commit produced this behaviour, and
what was the state of the board when it did?* A rule that does not help answer
that question is not in this document.

---

## What the repository is

`main` is the state of record. A claim is true of the project when it is true of
`main`, and the history is what allows behaviour on a bench to be traced back to
a driver, a `sdkconfig`, and a board revision.

Five things live here, and they do not overlap:

| Where | What | Changes when |
|---|---|---|
| `todos/` | the phases, their deliverables, the Decision Log and Risk Tracker | the specification changes |
| `hardware/` | schematic, PCB, BOM, fabrication outputs | the board changes |
| `firmware/` | the ESP-IDF application and its tests | behaviour changes |
| `software/` | host-side tooling and services | tooling changes |
| `docs/adr/` | decisions and why they were taken | a decision is taken or superseded |

Status lives in none of them. Status lives in the issue tracker, per
[`issue_sop.md`](./issue_sop.md).

---

## Branches

`main` is always buildable and always the state of record.

**Work directly on `main`** when the change is documentation or repository
furniture. A branch for a typo is ceremony, and ceremony practised alone decays
into ceremony ignored.

**Branch** when the work can leave the repository in a state where something in
it does not build, or when it will span more than one session. Firmware, KiCad,
and CI always branch.

**Naming: `<type>/<slug>`**, with the type drawn from the commit types below.

```
feat/mcp23017-driver
hw/v0.2-schematic
fix/ssd1306-init-order
refactor/drivers-and-hal
chore/…  docs/…  adr/…  spike/…
```

> The branches already on `origin` use this shape but not one type vocabulary —
> `features/ads1115` and `feature/dummy-measurement` differ by one letter,
> `driver/mcp23017` and `arch/firmware-layering` name a subsystem where the
> others name a kind of work, and `test/i2c-functional-test` says the word twice.
> They are all merged. Delete them rather than rename them; the vocabulary above
> applies to the next branch, not retroactively.

`spike/` is throwaway by declaration. A spike branch is never merged; what
survives it is rewritten on a branch of another type. This is the only way to
explore without the exploration entering the record as if it were considered.

**Merge with `--no-ff`.** The branch boundary is the record of what was done as
one piece of work. A fast-forward erases it, and a merge commit costs nothing.

**Delete the branch after merging**, locally and on `origin`. A merged branch
left standing is indistinguishable at a glance from work in progress.

---

## Commits

Conventional Commits, already in use here. Subjects are imperative — *correct*,
not *correcting* — because git's own generated subjects are imperative and a
mixed log reads as two authors.

**Scope is the arm: `<type>(hardware|firmware|software)`.** In a monorepo a `fix`
with no scope does not say whether a board or a build was wrong, and
`fix(hardware)` and `refactor(firmware)` are already in the log.

| Type | For |
|---|---|
| `feat` | new capability in firmware or host tooling |
| `fix` | corrects behaviour that was wrong |
| `refactor` | changes structure without changing behaviour |
| `test` | adds or corrects the Unity suite |
| `spec` | edits `todos/prototyping_todo.md` |
| `hw` | KiCad schematic, PCB, BOM, fabrication outputs |
| `adr` | adds or supersedes an entry in `docs/adr/` |
| `docs` | prose |
| `ci` | anything under `.github/workflows/` |
| `chore` | tooling, pins, repository furniture |

`hw` is a separate type from `feat` because a hardware change is the only kind
that cannot be reverted by deploying a new build, and it is the type the history
will be filtered on when a board misbehaves.

### Two atomicity rules

**A specification change and the result it grades never share a commit.** A
deliverable edited alongside the measurement that satisfies it is a deliverable
that could have been edited to fit the measurement, and nothing in the diff
distinguishes the two cases. When a deliverable turns out to be wrong, the `spec`
commit lands first, on its own, carrying its reason.

**A test change and the behaviour change it grades never share a commit.** A test
edited alongside the code it judges is a test that could have been edited to fit
the code, and nothing in the diff distinguishes the two cases. When a test turns
out to be wrong, the `test` commit lands first, on its own, carrying its reason.

**One hardware change per commit.** Two schematic edits in one commit cannot be
reverted separately, and the first thing wanted from a change that later looks
wrong is to remove it without removing its neighbour. KiCad files make this
harder than it sounds — see
[`README-kicad-git-workflow.md`](../../hardware/fortunelabs_mainboard_v0/README-kicad-git-workflow.md).

### The body of a gate-closing commit

Names what cannot be recovered from the diff:

```
fix(firmware): take the I2C timestamp at the edge, not after the read

pio test -e native
CI run: <url>
Prediction held: the read is out of the interrupt path.

Closes #7
```

### Standing rules

- **No measured figure from a board appears in a commit message before the gate
  that qualifies the measurement is closed.** Until then the board is a thing
  that produces numbers, and a number in a commit message is quoted long before
  it is qualified.
- **A generated artifact is never edited by hand.** Gerbers, `bom.csv`, and the
  ERC/DRC reports are outputs of `kicad-cli`. A hand-edited artifact is not an
  artifact; if one is wrong, fix the source and re-export.
- **`sdkconfig.defaults` is the pin; the generated `sdkconfig` is not.** The
  committed `sdkconfig` currently targets `esp32` while the board carries an
  ESP32-S3. That is tracked as an anomaly, not fixed by editing the generated
  file on the way past.
- **No credential is committed.** `main.c` currently carries a WiFi SSID and
  password in a `[DEV ONLY]` fallback block, in a public repository. Anything
  that has been pushed is disclosed and stays disclosed; rotate it, then move
  provisioning into NVS.

### History

Amend and rebase freely while a commit is unpushed. Once pushed, never — even
alone. The reason is not collaboration; it is that a pushed commit is the only
copy that survives this machine, and the SHA may already be cited from an issue.

---

## Tags

Annotated tags, marking points the physical world can be compared against:

- **`v<N>-fab`** — on the commit carrying the exact Gerbers sent for fabrication.
  Boards will exist matching one commit, and when board seven behaves differently
  from board two, this tag is what says they should not have.
- **`phase<N>-closed`** — every issue for the phase is closed and its Deliverable
  is satisfied. Phase 6 carries the go/no-go for PCB design, so its tag is the
  one that licences Phase 7.

There are no tags in this repository yet. `v0-fab` is owed to whichever commit
the existing boards were built from, and it gets harder to identify every week.

---

## What is tracked

Two `.gitignore` files, and they have different jobs. The root file covers OS and
editor droppings. `hardware/fortunelabs_mainboard_v0/.gitignore` governs KiCad,
where the rules are not obvious and the reasoning sits next to them.

Tracked deliberately, against the instinct to ignore them: `todos/`,
`sdkconfig.defaults`,
`dependencies.lock`, fabrication outputs, and the KiCad backups directory.

---

## Architecture decision records

**Write one** when a decision would be expensive to reverse, or when a future
reader would otherwise attribute the outcome to accident rather than to a choice.
Rejected alternatives are the payload; a record that lists only what was chosen
has recorded nothing.

**Filename:** `docs/adr/YYYY-MM-DD-<kebab-slug>.md`, the date being the date of
the decision, not of the writing.

**Status:** `Proposed`, `Accepted`, or `Superseded by <file>`.

**Never edit the Decision section of an Accepted record.** Supersede it with a
new file that names what survives from the old one and what changes. An edited
decision destroys the evidence that the project once believed otherwise, which is
the part worth keeping.

**The ADR is written from the argument, not from memory.** The argument is held
in a `type:decision` issue and the record is committed while it is still open.
See [`issue_sop.md`](./issue_sop.md).

One ADR is owed now: **the I²C clock.** The task list specifies 400 kHz, the bus
config in `main.c` sets 100 kHz, SSD1306 registers itself at 400 kHz, and the
Risk Tracker names exactly that drop to 100 kHz as the fallback for bus
capacitance. The bus-level figure is inert — clock is applied per device — so the
code cannot say what the project's clock actually is, let alone whether the
fallback was taken deliberately. That is a choice a future reader will read as an
accident, which is the test for writing a record.

The `esp32` `sdkconfig` on an ESP32-S3 board is **not** an owed ADR. Nobody chose
it; it is stale from the `hello_world` template and `idf.py set-target esp32s3`
rewrites it. It is a deviation, listed as one in
[`firmware/Readme.md`](../../firmware/Readme.md), and it closes as a
`type:anomaly` issue. An ADR records a decision that was taken — writing one for
a state nobody decided would dignify a defect as a choice.

The inner-vtable decision — implemented flat, then reverted — is recorded in
[`2026-07-21-ic-driver-inner-vtable.md`](../adr/2026-07-21-ic-driver-inner-vtable.md).
It was written after the fact, from a changelog that has since been deleted from
the repository root, which is the argument for holding the next one in a
`type:decision` issue while it is still live.

---

## Pull requests

Optional, and self-merged. The value of a PR alone is not review; it is a stable
URL carrying a diff that an issue can cite, and it is what runs CI before `main`
sees the change.

Open one when the branch touches firmware or hardware, or when the diff is too
large to read in one sitting. Otherwise merge the branch directly with `--no-ff`.

---

## Push cadence

**Push at the end of every session.** The failure mode for one developer is a
dead disk, not a merge conflict.

Before pushing: `git status` shows nothing unexpected, and no build output or
KiCad lock file appears that a commit did not intend.
