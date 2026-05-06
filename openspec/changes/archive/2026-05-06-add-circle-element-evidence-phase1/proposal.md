## Why

The visual element pipeline now has a runtime BEV raster fact surface and generic evidence records, but it still has no circle evidence detector. Adding circle recognition first, without line-following takeover, gives the next circle-entry work a small and reviewable visual fact contract.

## What Changes

- Add Phase 1 circle element recognition that produces `circle_left` and `circle_right` evidence from the current-frame `BEVElementRasterFrame`.
- Define the minimal visual rule: `cross_exit` requires bilateral opening plus strict wide-white-row support; `circle_left` is left-side opening plus right-side straight; `circle_right` is right-side opening plus left-side straight; one side opening with opposite shrink/non-straight support is bend/non-circle; neither side opening is straight or ordinary curve evidence.
- Keep circle detection independent from cross and control owners: the circle detector reads only raster facts and does not read cross evidence, line candidates, hold memory, safety, IMU, encoder, yaw, actuator, or control phase.
- Register circle in `steering_visual_element_pipeline.*` as four generic records: `circle_left_raw`, `circle_right_raw`, `circle_left`, and `circle_right`.
- Apply cross suppression only in the visual element pipeline aggregation layer: `cross_exit.present=true` preserves raw circle records while marking effective circle records absent with `reason=suppressed_by_cross_exit`.
- Add append-only `BEV_ELEMENT` parameters for strict cross wide-row support and circle evidence thresholds while preserving `CROSS_EXIT_TAKEOVER_ENABLED`.
- Keep Phase 1 evidence-only: circle records publish candidate summaries with `built=false`, `takeover_enabled=false`, `included_in_arbitration=false`, and `reason=evidence_only`, and no circle `VisualReferenceCandidate` is pushed.
- Update offline probe support so authority-baseline raw images can exercise the same runtime raster-backed circle input path used by the runtime pipeline.

## Capabilities

### New Capabilities

- None. Circle recognition extends the existing BEV visual element evidence capability instead of introducing a separate runtime authority.

### Modified Capabilities

- `bev-visual-element-evidence`: add raster-backed circle evidence records, raw/effective suppression semantics, Phase 1 candidate non-takeover semantics, and circle evidence parameters.

## Risk Tier

- `STANDARD`: this change touches visual perception/evidence code, runtime parameters, config defaults, probe tooling, tests, and public debug/telemetry/media evidence records. It intentionally does not change ordinary line selection, hold, usability, lateral error, readiness, safety, yaw, actuator behavior, or circle/cross candidate arbitration.

## Impact

- Affected layers: `port` evidence/parameter contracts, `legacy` circle detector and element pipeline aggregation, `runtime` perception input wiring by existing raster path, `platform` parameter loading and evidence serialization as needed, `config` default parameters, `verification` tests and authority-baseline probe coverage, and root README/OpenSpec docs.
- Public interfaces: `RuntimeParameters.BEV_ELEMENT` gains append-only circle fields; `element_evidence.records` gains circle record ids using the existing generic record wire shape.
- Dependencies: depends on the completed `establish-bev-element-raster-pipeline` change for `BEVElementRasterFrame`, `steering_visual_element_pipeline.*`, and generic `VisualElementEvidenceRecord` support.
- No new external dependencies, no board-motion requirement for artifact review, and no actuator-control behavior change in Phase 1.
