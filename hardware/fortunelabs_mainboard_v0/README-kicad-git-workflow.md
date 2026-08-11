# KiCad + Git Workflow

A minimal template for managing a KiCad (v6+) project with git.

## Folder structure

```
project-root/
├── .gitignore
├── .github/workflows/kicad-ci.yml
├── project.kicad_pro
├── project.kicad_sch
├── project.kicad_pcb
├── libraries/
│   ├── symbols/
│   ├── footprints.pretty/
│   └── 3dmodels/
└── docs/
    └── (datasheets, design notes, etc.)
```

## Initial setup

```bash
git init
cp .gitignore /path/to/project/
mkdir -p .github/workflows
cp kicad-ci.yml .github/workflows/
git add .
git commit -m "Initial commit: project skeleton"
```

Open KiCad and enable **Tools > Git** in the Project Manager to commit/push/pull
directly from the GUI (available since KiCad 7). This helps because KiCad knows
which files are relevant and skips caches and backups automatically.

## Commit habits

Commit one logical change at a time: "add 3.3V regulator", "route power plane",
"swap USB-C connector footprint". Avoid a single large commit covering many
changes. Schematic and PCB diffs are text-based but dense with coordinates, so
small commits make review and revert far easier.

## Reviewing changes (visual diff)

Raw text diffs of `.kicad_sch`/`.kicad_pcb` are hard to read. Use one of:

- `kicad-cli sch export pdf` / `kicad-cli pcb export pdf` on two commits, then
  compare the PDFs.
- A third-party plugin such as `kicad-diff` (renders a PNG per commit for a
  side-by-side visual diff).
- KiCad 8's built-in "Compare" when browsing git history in the GUI.

## Libraries

For custom symbols and footprints, put them in `libraries/` inside the same repo
(for small projects), or make them a separate repo plus a git submodule (to
share across projects):

```bash
git submodule add https://github.com/username/kicad-libs.git libraries/shared
```

Register the library in the project-specific library table (Preferences > Manage
Symbol/Footprint Libraries > choose "Project", not "Global") so paths stay
relative and reproducible on another machine.

## Branching

- `main`: always ERC/DRC clean and ready for fabrication.
- `feature/<name>`: work in progress (e.g. adding a sensor, changing a regulator).
- `rev-b`, `rev-c`, …: when you want to mark physical board revisions explicitly.

Merge into `main` once ERC/DRC passes. If a schematic branch has diverged far,
conflicts in the S-expression files are painful to resolve by hand, and it is often
faster to pick one version and manually re-apply the important changes than to
merge automatically.

## Release tagging

Tag every revision actually sent to the fab:

```bash
git tag -a v1.0-rev-a -m "Rev A sent to JLCPCB 2026-07-21"
git push origin v1.0-rev-a
```

This makes the gerbers the fab received traceable back to an exact commit.

## CI (optional, already set up in `.github/workflows/kicad-ci.yml`)

On every push/PR to `main` that touches `hardware/`, the workflow automatically:

1. Runs `kicad-cli sch erc` and `kicad-cli pcb drc`.
2. Exports gerbers, drill files, and the BOM as artifacts.

The workflow lives at the repository root, not in this directory, because GitHub only
reads `.github/workflows/` at the top level. Its steps run with
`working-directory: hardware/fortunelabs_mainboard_v0`, so update both that
setting and the `.kicad_sch`/`.kicad_pcb` filenames in the `run:` steps if you
reuse this template for another project.
