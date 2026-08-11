<!--
Self-merged. The value of a PR alone is not review; it is a stable diff an issue
can cite, and it is what runs CI before main sees the change. Open one when the
branch touches firmware or hardware, or when the diff is too large to read in one
sitting. Otherwise merge with --no-ff and skip this. See docs/sop/git_sop.md.
-->

## What this changes

## Issue

Closes #

## Checks

- [ ] No test change shares a commit with the behaviour it grades
- [ ] One hardware change per commit
- [ ] No generated artifact edited by hand (Gerbers, `bom.csv`, ERC/DRC reports, `sdkconfig`)
- [ ] No measured figure from a board quoted before the gate that qualifies it closed
- [ ] CI green, and the run is cited on the issue this closes
