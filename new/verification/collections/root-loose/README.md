# Root Loose Verification Artifacts

This directory groups the loose top-level verification artifacts under `new/verification/` without relocating the originals.

## Why Links Instead Of Moves

Many of the phase-B and phase-D logs and CSVs are referenced directly by:

- `new/verification/*.md`
- `new/docs/*`
- `openspec/*`
- shell commands that write to hard-coded paths under `new/verification/`

To avoid breaking those references, the original files stay in place and this directory provides grouped entrypoints only.

## Groups

- `phase-b/`
- `phase-d/`
- `diag/`
- `misc/`
