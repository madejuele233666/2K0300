# Vendor Baseline

## Accepted Baseline

- Active vendor root: `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library`
- Active build skeleton: `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/project/user`
- Active verification target: the retarget change `openspec/changes/retarget-port-to-true-ls2k0300-library/`

## Superseded Baseline

- Historical path only: `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library`
- Historical change only: `openspec/changes/port-old-to-new-ls2k0300-library/`
- These artifacts remain useful as contrast evidence for the wrong target selection, but they are not accepted phase-1 implementation or verification sources for the real port.

## Practical Rules

- `new/user/CMakeLists.txt`, `new/user/build.sh`, and `new/user/run_remote_smoke.sh` must resolve vendor files from the accepted baseline only.
- `.hpp` wrapper assumptions from the superseded baseline are invalid for the real retarget unless explicitly revalidated against the true tree.
- Vendor headers, globals, free functions, and direct vendor C++ types must stay confined to platform-owned bridge code during the retarget.
