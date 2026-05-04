# Port Type Include Boundaries

## Rule

Do not create aggregate include headers for steering/reference/control types.

## Layered Type Headers

- `camera_frame_types.hpp`: camera frame view/capture types.
- `bev_geometry_types.hpp`: BEV points, calibration, geometry, classification, and control-model parameters.
- `bev_reference_types.hpp`: reference path facts, point source, hold state, and continuity result.
- `visual_reference_orchestration_types.hpp`: visual reference candidate and current visual selection summary.
- `reference_usability_types.hpp`: reference usability result.
- `reference_lateral_error_types.hpp`: reference weighted lateral-error result.
- `reference_control_readiness_types.hpp`: selected reference and lateral-error readiness result.
- `perception_result.hpp`: runtime steering pipeline transport snapshot.
- `steering_state_types.hpp`: owner-specific steering memory.
- `sensor_sample_types.hpp`: IMU, encoder, and low-voltage samples.
- `actuator_command_types.hpp`: actuator command.
- `runtime_parameter_types.hpp`: runtime parameter container.

## Allowed Dependencies

- BEV projector includes only `bev_geometry_types.hpp` and `camera_frame_types.hpp`.
- BEV simple perception includes camera frame, BEV geometry, BEV reference, and runtime parameter types.
- Visual-reference orchestration includes BEV reference and visual-reference orchestration types.
- Reference usability includes BEV reference, reference usability, and runtime parameter types.
- Reference lateral error includes BEV reference, reference usability, reference lateral-error, and runtime parameter types.
- Reference-control readiness includes reference-control readiness, reference usability, and reference lateral-error types.
- Otsu threshold includes only camera frame types.
- Perception frontend includes camera frame, perception result, steering state, sensor sample, runtime parameter types, and layer function headers.
- Steering yaw target includes steering state and runtime parameter types.
- Protocol, media, and debug snapshot layers may include `perception_result.hpp` or their own protocol view types.
- Tests include the concrete layer under test.

## Forbidden Dependencies

- Active code must not include `port/control_types.hpp`.
- Reference usability must not include `perception_result.hpp`.
- Reference lateral error must not include `perception_result.hpp`.
- Steering yaw target must not include `perception_result.hpp`.
- Reference-control readiness must not depend on low voltage, projector state, IMU, encoder, or stale timing.
- BEV simple perception and reference hold builders must not depend on reference usability.
- Runtime frontend is the owner of current/hold/none reference selection.
- Visual-reference orchestration is the only owner of current visual reference candidate selection.
- Reference continuity remains the only owner of hold selection.
- Assistant/debug transports must serialize published facts and must not recompute control facts.
- Safety gate is the only owner of low voltage, perception health, stale, IMU, and encoder veto.
- Low-voltage raw thresholds belong to the power adapter / low-voltage sampler owner, not perception, reference, readiness, or yaw.
- Steering yaw target must not depend on readiness or control validity.
- `source` and `mode` may be serialized or drawn, but must not decide usability, lateral error, reference-control readiness, safety gate, or yaw target.

PerceptionResult is a runtime transport snapshot, not a dependency shortcut.

## Memory Ownership

- `PerceptionFrontend` owns `SteeringPerceptionMemory`: reference hold only.
- IMU health belongs to the control/safety path; perception must not own IMU safety memory or IMU grace state.
- `ControlLoop` owns `SteeringControlMemory`: yaw controller memory.
- `RuntimeState` does not store mixed steering memory. Runtime reset requests for perception memory use `perception_memory_reset_generation`.
- `PerceptionResult` is assembled only by the runtime frontend from layer facts.
- Old camera PID parameters and the removed adaptive camera controller are absent from active code and JSON.
