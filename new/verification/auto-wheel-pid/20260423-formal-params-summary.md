# 2026-04-23 Wheel PID Formal Params Summary

This note closes the current wheel PID tuning round and records the initial formal parameters promoted into `new/config/default_params.json`.

## Promoted Params

- Left wheel:
  - `P = 84.0`
  - `I = 2.4`
  - `D = 0.75`
  - `INTEGRAL_LIMIT = 2200.0`
  - `MEASUREMENT_FILTER_ALPHA = 0.4`
- Right wheel:
  - `P = 96.0`
  - `I = 2.2`
  - `D = 0.2`
  - `INTEGRAL_LIMIT = 2200.0`
  - `MEASUREMENT_FILTER_ALPHA = 0.4`

## Why This Set

- Final `I` working center:
  - left fixed at `2.4`
  - right refined around `2.2`
- Final `P` working center after `I/D` were fixed:
  - left shifted down from `100` into the low-`80` range
  - right stayed near the mid-`90` range
- The combined working center `left P=84, right P=96, left I=2.4, right I=2.2, left D=0.75, right D=0.2` produced the cleanest end-of-round baseline among the explored asymmetric combinations.

## Key Evidence

- `left I=2.4`, `right I=2.2` refinement:
  - [20260423T124335Z/run-summary.md](./20260423T124335Z/run-summary.md)
- `P` down-scan under the fixed `I/D` set:
  - [20260423T124827Z/run-summary.md](./20260423T124827Z/run-summary.md)
- Left-down/right-near-96 follow-up:
  - [20260423T125309Z/run-summary.md](./20260423T125309Z/run-summary.md)

## Residual Risk

- No candidate achieved fully repeat-stable multi-run confirmation on every late-stage replay.
- Treat this promotion as an initial formal set for continued vehicle validation, not a final locked calibration.
