## Why

Current steering control turns the selected BEV reference path into a single weighted future lateral value, then maps that value directly to turn output. That mixes current tracking offset with path shape, so bends and circle references can be under- or over-steered without a clean way to tune the separate geometric causes.

## What Changes

- Add a neutral reference tracking geometry layer after selected/held/time-aligned reference selection and before reference-control readiness.
- Compute `lateral_offset_m`, `heading_error_rad`, and `curvature_m_inv` from the same selected reference path without reading candidate kind, CircleV2 state, cross evidence, or arbitration policy.
- Update reference-control readiness to depend on computed tracking geometry instead of treating the old weighted lateral error as the only steering-control prerequisite.
- Update yaw turn target computation to consume tracking geometry and expose separate lateral, heading, and curvature terms.
- Expose tracking geometry and yaw-term decomposition through steering debug/media/assistant evidence for tuning.
- Preserve wheel mixer, wheel PID, PWM, path generators, visual reference selection, CircleV2 FSM, and element recognition boundaries.

## Capabilities

### New Capabilities
- `reference-tracking-geometry`: Defines the selected-reference geometry facts, readiness contract, yaw-control input contract, and tuning parameter surface for lateral offset, heading error, and curvature.

### Modified Capabilities
- `steering-tuning-media-observability`: Adds public steering snapshot/media evidence for `tracking_geometry` and yaw-control term decomposition so runs can explain the new control law.
- `assistant-telemetry-sidecar`: Adds the same read-only tracking geometry and yaw-term facts to assistant telemetry while preserving the existing command/session boundary.

## Risk Tier

- `STANDARD`: The change touches runtime control behavior, runtime parameter parsing/defaults, telemetry serialization, and tests, but it stays inside existing steering-control layering and does not change platform actuator APIs, image acquisition, visual element detection, visual reference arbitration, wheel mixer, wheel PID, or PWM output ownership.

## Impact

- `port`: new reference tracking geometry type; `PerceptionResult` carries the computed geometry; BEV control-model parameters expose separate tracking gains and fit minimum.
- `legacy`: new neutral geometry helper; reference-control readiness checks tracking geometry; yaw controller consumes tracking geometry and reports lateral/heading/curvature terms.
- `runtime`: perception and control-time alignment compute geometry from the selected reference; control debug snapshot/reporting serializes the new facts.
- `platform`: parameter store and steering media protocol publish the new control-model keys and evidence fields.
- `config`: defaults and parameter docs gain the new tracking control parameters.
- `user`/tests: media selftest, scene overlay probe, and steering/control tests validate the new facts and term decomposition.
