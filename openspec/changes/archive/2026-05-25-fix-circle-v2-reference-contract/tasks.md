## 0. Verification Contract

- [x] 0.1 Run docs-first validation for `fix-circle-v2-reference-contract`.
- [x] 0.2 Run two source-first verification loops after implementation; each loop must include authoritative findings/evidence artifacts under this change.
- [x] 0.3 Sync delta specs to main specs after both verification loops pass.
- [x] 0.4 Archive the change after sync and task completion.

## 1. Circle V2 Reference Contract Fix

- [x] 1.1 Tighten `CircleV2GeometryObserver` so `InnerTrace` geometry requires a finite leading-contiguous path segment rather than `present_count > 0`.
- [x] 1.2 Preserve `ExitTrace` straight-edge behavior while also requiring a leading-contiguous segment.
- [x] 1.3 Keep `CircleV2ReferenceComposer` thin: it offsets only available geometry and does not repair gaps.
- [x] 1.4 Keep `CircleV2ReferenceAdapter` thin: absent plan returns no candidate and present plan maps fixed role/direction/source.

## 2. Tests

- [x] 2.1 Add tests proving one-point `InnerTrace` geometry produces no plan and no adapted candidate.
- [x] 2.2 Add tests proving gapped `InnerTrace` geometry produces no plan.
- [x] 2.3 Add tests proving contiguous `InnerTrace` geometry still produces the expected offset plan and adapter mapping.
- [x] 2.4 Run focused Circle V2 and visual-reference tests plus `git diff --check`.
