# TC264 To True LS2K0300 Mapping

## Retained phase-1 contracts

| Legacy source | New location | Phase-1 action |
|---|---|---|
| `old/code/FUZZY_PID_UCAS.c/.h` | `new/code/legacy/fuzzy_pid_ucas.cpp` + `new/code/legacy/fuzzy_pid_ucas.hpp` | Retained fuzzy steering table contract (`InitMH`, `DuoJiGetP`) with `P_Mode` selection. |
| `old/code/PID.c` | `new/code/legacy/pid_control.cpp` | Retained camera+gyro turn and mean-speed PID structure with explicit state ownership. |
| `old/code/Motor.c` | `new/code/legacy/motor_logic.cpp` + `new/code/platform/motor_adapter.cpp` | Legacy mix logic kept; direct PWM/GPIO writes moved into adapter. |
| `old/code/ZiTaiJieSuan.c` | `new/code/legacy/attitude_logic.cpp` + `new/code/platform/imu_adapter.cpp` | Attitude update retained as a runtime helper fed by adapter samples. |
| `old/code/camera.c` (selected subset) | `new/code/legacy/camera_logic.cpp` + `new/code/runtime/perception_frontend.cpp` | `otsuThreshold_fast`-style thresholding and `eight_neighbor`-equivalent foreground path retained/replaced explicitly. |

## True-baseline bridge ownership

- `new/code/platform/true_ls2k0300/camera_bridge.cpp`: owns `uvc_camera_init`, `wait_image_refresh`, and vendor grayscale buffers.
- `new/code/platform/true_ls2k0300/imu_bridge.cpp`: owns `imu_get_dev_info`, `imu_type`, and device-specific IMU globals/getters.
- `new/code/platform/true_ls2k0300/encoder_bridge.cpp`: owns `encoder_get_count` and absolute-count sampling.
- `new/code/platform/true_ls2k0300/motor_bridge.cpp`: owns `gpio_set_level` and `pwm_set_duty`.
- `new/code/platform/true_ls2k0300/adc_bridge.cpp`: owns `adc_convert`.
- `new/code/platform/true_ls2k0300/timer_bridge.cpp`: owns `Pit_timer` and direct PIT callback wiring.

## Foreground frame-ready mapping (`cpu0_main.c`)

- Frame-ready ownership moved from `old/user/cpu0_main.c` into `new/code/runtime/perception_frontend.cpp`.
- `otsuThreshold_fast` is retained as `OtsuThresholdFast`.
- `eight_neighbor` is replaced by an explicit documented equivalent: weighted lane-center extraction over thresholded rows (`EightNeighborEquivalentError`).
- Threshold/low-voltage emergency behavior is retained as `threshold_veto` + `low_voltage_veto` and published before control consumption.
- Main-loop orchestration moved to `new/user/main.cpp` as a restricted composition root.

## Periodic control mapping (`isr.c`)

- PIT ISR behavior from `old/user/isr.c` maps to `new/code/runtime/control_loop.cpp` through `ITimerAdapter`.
- Explicit initialized carry-over state from ISR globals:
  - `W_Target_last` -> `new/code/runtime/runtime_state.hpp::W_Target_last`
  - `bcount` -> `new/code/runtime/runtime_state.hpp::bcount`
  - `circle_find`, `zebra_flag`, `cross_flag` -> `new/code/runtime/runtime_state.hpp`
- Runtime low-voltage veto is re-sampled through `new/code/platform/power_adapter.cpp` and surfaced by `power.low_voltage.transition`.

## Deferred or collapsed phase-1 scope

- `old/code/Servo.c`: deferred (motor/servo parity beyond safe actuator output is out of phase 1).
- `old/code/All_init_core1.c`: deferred (dual-core split collapsed into single-process runtime).
- `old/user/cpu1_main.c`: deferred (phase-1 runs one process with one periodic timer service).
- Nested interrupt semantics: deferred and explicitly not reproduced.
- Flash/TFT interactive tuning parity: replaced with file-backed config in `new/config/default_params.json`.

## Parameter field inventory

Phase-1 persists and consumes:

- `Speed_base`
- `JWJC`
- `circle_k`
- `circle_b`
- `road_k`
- `road_b`
- `see_max`
- `PID_TURN_CAMERA.D`
- `PID_TURN_GYRO_CAMERA.D`
- `Straight_permit`
- `island_point`
- `island_delay`
- `circle_k_err`
- `P_Mode`
- `exp_light`

`P_Mode` and `exp_light` are applied by `ParamStore::ApplyStartupCritical` before runtime bring-up and actuator arming. On the true direct-match camera path, non-default `exp_light` requires an explicit adaptation hook because the vendor public API does not expose direct exposure control.
