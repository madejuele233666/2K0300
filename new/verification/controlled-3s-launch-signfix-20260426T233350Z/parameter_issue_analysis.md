# Steering Parameter Issue Analysis

Evidence source: `controlled-3s-launch-signfix-20260426T233350Z`

## Finding

The 3s run does not show a remaining turn-sign problem. It shows an over-aggressive steering response after the sign fix.

The active control equation is:

`candidate = P * near_lateral_error + P * 0.55 * far_heading_error + P * 0.35 * preview_curvature * speed_scale`

then:

`w_target = 0.9 * candidate + 0.1 * previous_w_target`

and:

`raw_turn_output ~= PID_TURN_GYRO_CAMERA.P * w_target`

because `gyro_z` is only around `0.1..1.6` while `w_target` is hundreds to thousands.

## Evidence

Selected samples using the current params:

- `PID_TURN_CAMERA.P=12000`
- `FAR_HEADING_WEIGHT=0.55`
- `PREVIEW_CURVATURE_WEIGHT=0.35`
- `PID_TURN_GYRO_CAMERA.P=0.5`

| frame | phase | near | heading | curvature | near term | heading term | curvature term | candidate | raw |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 24 | RUNNING | 0.0072 | 0.0606 | 0.0210 | 86 | 400 | 88 | 575 | 286 |
| 32 | RUNNING | 0.0270 | 0.0715 | 0.0183 | 324 | 472 | 77 | 873 | 433 |
| 67 | RUNNING | 0.0453 | -0.0454 | -0.0057 | 543 | -300 | -24 | 220 | 110 |
| 72 | RUNNING | 0.0090 | -0.0366 | -0.1204 | 108 | -241 | -506 | -640 | -291 |
| 96 | RUNNING | -0.0898 | -0.0981 | -0.0240 | -1078 | -647 | -101 | -1826 | -912 |
| 104 | RUNNING | -0.1173 | -0.0975 | -0.0250 | -1407 | -643 | -105 | -2155 | -1077 |

Interpretation:

- At frame 24, lateral error is only `0.0072m`, but heading contributes about 70% of the absolute control input.
- At frame 72, lateral error is still positive, but preview curvature flips strongly negative and contributes about 59% of the absolute input, forcing `raw_turn_output` negative.
- Late RUNNING then drives the opposite direction hard: `raw_turn_output` reaches `-1077`.

## Parameter Assessment

Primary issue:

- `PID_TURN_CAMERA.P=12000` is too aggressive when applied to the fused BEV error vector, not only to near lateral error.

Secondary issue:

- `PREVIEW_CURVATURE_WEIGHT=0.35` is too influential for the current sparse geometry noise/transition behavior. It can flip the command sign before the near lateral term agrees.

Not the primary issue:

- `raw_turn_output_limit=8000` is not active; the observed outputs are far below the clamp.
- `wheel_turn_target_scale=100` is not the root cause; wheel targets and encoder feedback follow the requested split.
- `PID_TURN_CAMERA.D` cannot fix this by tuning, because it is deprecated and ignored by the current outer loop.

`PID_TURN_GYRO_CAMERA.P=0.5` currently behaves mostly as an output scale. Lowering it would reduce raw turn output, but it would not add real damping because `gyro_z` is much smaller than `w_target` in this contract.

## Recommended Next Parameter Move

Use one coherent conservative move, then rerun a short controlled test:

- `PID_TURN_CAMERA.P`: `12000 -> 8000`
- `BEV_CONTROL_MODEL.PREVIEW_CURVATURE_WEIGHT`: `0.35 -> 0.15`
- Keep `PID_TURN_GYRO_CAMERA.P=0.5`
- Keep `FAR_HEADING_WEIGHT=0.55` for the first retest

Expected effect:

- reduce global steering authority by about one third;
- reduce abrupt curvature-driven sign flips by more than half;
- keep enough near/heading authority for right/left correction at `Speed_base=100`.

Do not extend run duration again before this is retested.
