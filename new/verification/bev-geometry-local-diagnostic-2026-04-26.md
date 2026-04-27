# BEV Geometry Local Diagnostic - 2026-04-26

## Inputs

- Straight-center frame:
  - `new/verification/bev-straight-center-wireless-reset-20260426T122803Z/steering-media/frames/frame-000371.raw`
- Bend-entry frame:
  - `new/verification/bev-bend-entry-wireless-20260426T122106Z/steering-media/frames/frame-000419.raw`
- Runtime state:
  - board was kept `DISARMED`
  - no motor start, speed, or tuning command was sent

## Finding

The raw images expose clear road-surface runs, but the previous projector calibration mapped those runs to an expanding BEV lane width:

- straight-center, `FORWARD_SAMPLE_1=0.55m`: projected lane width `0.785m`
- straight-center, `FORWARD_SAMPLE_7=2.30m`: projected lane width `1.907m`
- bend-entry, `FORWARD_SAMPLE_1=0.55m`: projected lane width `0.773m`
- bend-entry, `FORWARD_SAMPLE_7=2.30m`: projected lane width `1.976m`

Because `BEV_GEOMETRY.MAX_LANE_WIDTH_M=0.75`, only the nearest sample survived. The resulting `valid_count=1` made the track invalid and left `visible_range_m` at about `0.34m`.

## Root Cause

The issue is a projector calibration contract mismatch, not a threshold miss:

- thresholded rows produced stable white road runs
- the source calibration points were inside the observed road boundaries for the current camera pose
- the target lateral width widened with distance, which made geometry width validation reject otherwise visible road samples

## Change

Updated the default projector calibration to use straight-center road boundaries:

- lower source row `220`: columns `24`, `299`
- upper source row `148`: columns `63`, `257`
- target half width: constant `0.21m`
- target forward rows kept at `0.45m` and `2.25m`

This keeps road width validation inside `BEV_GEOMETRY` and avoids encoding widening behavior into the projector.

## Expected Local Result

With the updated calibration, the same straight-center frame produces valid widths for all eight forward samples:

- width range: about `0.417m` to `0.423m`
- near lateral error: about `-0.001m`
- all eight samples pass the existing `MIN_LANE_WIDTH_M/MAX_LANE_WIDTH_M` gate

The bend-entry frame also produces valid widths for all eight forward samples:

- width range: about `0.390m` to `0.438m`
- near lateral error: about `0.043m`
- all eight samples pass the existing width gate
