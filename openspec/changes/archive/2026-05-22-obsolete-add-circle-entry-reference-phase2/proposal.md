## Why

Phase 1 can identify effective `circle_left/right` visual facts, but runtime still only follows the ordinary line reference because no circle entry reference candidate is produced. The next step is to convert the current-frame inner-frontier evidence into a default-off `VisualReferenceCandidate` without adding actuator control, state-machine behavior, or artificial lateral offsets.

## What Changes

- Extend circle detector typed output with circle entry facts derived from `BEVElementRasterFrame`: near-connected white component row centers/widths, rear-side black-white frontier chain, inferred road half-width, frontier-to-centerline points, direction delta, and reason.
- Preserve the existing generic record wire shape and fixed circle record order: `circle_left_raw`, `circle_right_raw`, `circle_left`, `circle_right`.
- Add a circle entry candidate builder that consumes effective circle evidence plus typed entry facts and emits `VisualReferenceCandidateKind::kCircleLeft` or `VisualReferenceCandidateKind::kCircleRight` only when the path is finite, continuous from index 0, and passes existing visual-reference validation.
- Keep circle takeover default-off with append-only `BEV_ELEMENT` parameters; when takeover is disabled, candidate construction may be reported in evidence but the candidate is not included in arbitration.
- Keep cross suppression in the visual element pipeline: raw circle facts remain observable, effective circle records become absent, and no circle candidate is built or pushed after suppression.
- Update offline probe and tests so authority-baseline circle samples can observe built-but-default-excluded circle entry candidates and takeover-enabled selection behavior.

## Capabilities

### New Capabilities

- `circle-entry-reference-candidate`: Covers conversion of effective circle evidence and typed entry facts into a default-off circle entry `VisualReferenceCandidate` based on the observed rear-side inner frontier and inferred road half-width.

### Modified Capabilities

- `bev-visual-element-evidence`: Extends circle evidence from Phase 1 raw/effective generic records to include typed circle entry facts, candidate summary semantics, append-only parameters, and pipeline candidate inclusion rules.

## Risk Tier

- `STRICT`: This change touches steering visual-reference candidate generation and, when explicitly enabled by parameter, can affect the selected reference that feeds lateral error, readiness, safety, yaw, and actuator chains. The default remains takeover disabled, but the implementation spans `port`, `legacy`, `runtime`, `config`, offline probe, and verification tests.

## Impact

- `port`: Extend project-owned visual element/reference data contracts for typed circle entry facts and append-only runtime parameters.
- `legacy`: Update circle evidence extraction, add circle entry candidate building, and update visual element pipeline aggregation/candidate summary behavior.
- `runtime`: Preserve the existing single-frame perception pipeline boundary while allowing enabled circle candidates to enter existing visual-reference orchestration.
- `config`: Add default-off circle entry parameters under `BEV_ELEMENT` without changing existing cross or Phase 1 circle fields.
- `new/user` and verification: Update `scene_overlay_probe` observability and tests for candidate construction, suppression, takeover gating, and authority-baseline replay.
- No new third-party dependencies or actuator-control APIs are introduced.
