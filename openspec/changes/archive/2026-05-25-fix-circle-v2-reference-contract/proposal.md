## Why

Circle V2 can currently enter `InnerTrace` and emit circle candidates even when the generated path contains only one leading sample or has gaps. That violates the V2 document's scene ownership contract: `CircleV2Scene` should output a reference plan only when the plan is already a usable scene-owned path, not rely on visual arbitration or reference usability to reject malformed circle paths later.

## What Changes

- Tighten the Circle V2 reference-plan contract so `InnerTrace` and `ExitTrace` only produce plans with a contiguous leading path segment.
- Keep geometry observation and reference composition separate: geometry decides whether role-specific edge facts are sufficient; composer only offsets already-valid geometry.
- Keep candidate adaptation simple and fixed: adapter wraps a present `CircleV2ReferencePlan` but does not decide geometry validity or infer confidence from internal facts.
- Prevent single-point and gapped Circle V2 paths from entering visual-reference arbitration as circle candidates.
- Add focused tests covering one-point inner geometry, gapped inner geometry, contiguous inner geometry, adapter behavior, and telemetry preservation.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sparse-circle-v2-scene`: tighten the reference-plan availability contract for Circle V2 geometry/composition and candidate adaptation.

## Risk Tier

- `STANDARD`: this changes active steering reference candidate generation, but the scope is limited to Circle V2 plan availability, adapter input validity, and focused tests. It does not add new public runtime parameters, new hardware dependencies, or a new control-loop mode.

## Impact

- Affected layers: `runtime` Circle V2 geometry/composer/adapter, `legacy` visual-reference orchestration interaction through existing candidates, and `verification` Circle V2 tests.
- Public interfaces: no new user-facing parameter or media field is required.
- Dependencies: uses existing BEV reference path sampling, existing `MIN_LEADING_REFERENCE_SAMPLES` semantics as the downstream usability model, and existing Circle V2 telemetry.
- No new external library dependency is expected.
