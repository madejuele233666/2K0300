## MODIFIED Requirements

### Requirement: Circle V2 Events Are Phase-Gated
`CircleV2EventObserver` SHALL translate visual and motion facts into transition events, and `CircleV2Reducer` SHALL read only those events. Phase1 circle cue and Approach entry gate SHALL derive from the same Circle scene internal locked-side expansion observation used by V3 entrance-corner estimation. The event observer SHALL gate event production by prior phase:

- `Idle`: only `detected_dir`
- `Approach`: only locked-direction `entry_gate_reached`
- `InnerTrace`: only `exit_gate_reached`
- `ExitTrace`: no detected, entry, or exit events

#### Scenario: Shared expansion observation preserves Phase1 cue parity
- **WHEN** prior phase is `Idle`
- **THEN** the event observer SHALL infer `detected_dir` from internal side-expansion observation
- **AND** the result SHALL match the old Phase1 circle cue for equivalent sparse-row input for `left`, `right`, and `none`
- **AND** the public `SceneFrameView` SHALL NOT expose left-open, right-open, bottom-expansion, or entrance-corner facts

#### Scenario: Side-specific observation ignores opposite-side jumps
- **WHEN** the shared expansion observation computes left-side reach, growth, or straight baseline
- **THEN** it SHALL consume only rows whose left boundary is actually on the left side
- **AND** a widest interval that jumps to the right side SHALL be treated as missing for left-side observation, not as a valid zero-reach left sample
- **WHEN** it computes right-side reach, growth, or straight baseline
- **THEN** it SHALL mirror the same rule for right-side boundaries

#### Scenario: Approach consumes only locked-direction expansion
- **WHEN** prior phase is `Approach` and locked direction is `left`
- **THEN** only left-side expansion from the shared expansion observation SHALL be allowed to set `entry_gate_reached`
- **AND** right-side expansion SHALL NOT move the FSM to `InnerTrace`
- **WHEN** locked direction is `right`
- **THEN** only right-side expansion SHALL be allowed to set `entry_gate_reached`

### Requirement: Circle V2 Reference Plans Replace Rear-Black Entry Paths
Runtime circle reference construction SHALL NOT use rear / side-rear black frontier facts. `InnerTrace` SHALL derive its entrance reference from a V3 fixed-slope guide: estimate the locked-direction outer entrance corner `P_est`, construct a virtual opposite boundary through `P_est` using the configured direction-specific fixed slope, and offset that virtual boundary by road half width toward the locked direction. `ExitTrace` SHALL derive its reference from the locked direction's opposite straight edge, offset by road half width toward the locked direction.

Runtime Circle V2 reference construction SHALL emit a `CircleV2ReferencePlan` only when the role-specific geometry forms a finite leading-contiguous path segment. Single-sample, gapped, or otherwise structurally incomplete observations SHALL be treated as unavailable geometry rather than adapted into circle visual-reference candidates.

For V3 `InnerTrace`, the virtual boundary SHALL be sampled at the leading finite `ordinary_road.center_path.sampled_path` forward coordinates, starting from index `0` and stopping at the first absent or non-finite center sample. Each virtual edge sample SHALL use the center sample's `forward_m` as `y` and the fixed-slope line equation as `x`. The resulting virtual edge segment SHALL be considered usable only when it has at least the implementation's required minimum leading contiguous sample count.

#### Scenario: InnerTrace uses fixed-slope entrance guide for left circle
- **WHEN** current-frame reference context is `InnerTrace` and direction is `left`
- **AND** the expansion observer can estimate the left-side outer entrance corner `P_est`
- **THEN** `CircleV2GeometryObserver` SHALL construct `virtual_right_edge = line_through(P_est, CIRCLE_V2_ENTRY_FIXED_SLOPE_LEFT_DX_DY)`
- **AND** `CircleV2ReferenceComposer` SHALL offset that virtual right edge leftward by `ordinary_road.half_width`
- **AND** it SHALL emit the resulting `CircleV2ReferencePlan` only when the leading samples are finite and contiguous

#### Scenario: InnerTrace uses fixed-slope entrance guide for right circle
- **WHEN** current-frame reference context is `InnerTrace` and direction is `right`
- **AND** the expansion observer can estimate the right-side outer entrance corner `P_est`
- **THEN** `CircleV2GeometryObserver` SHALL construct `virtual_left_edge = line_through(P_est, CIRCLE_V2_ENTRY_FIXED_SLOPE_RIGHT_DX_DY)`
- **AND** `CircleV2ReferenceComposer` SHALL offset that virtual left edge rightward by `ordinary_road.half_width`
- **AND** it SHALL emit the resulting `CircleV2ReferencePlan` only when the leading samples are finite and contiguous

#### Scenario: Entrance corner estimation separates forward and lateral coordinates
- **WHEN** V3 estimates `P_est` from sparse BEV rows
- **THEN** `P_est.y` SHALL come from the locked-side expansion component's far boundary in `forward_m`
- **AND** `P_est.x` SHALL come from the locked-side straight baseline at `P_est.y`
- **AND** `P_est.x` SHALL NOT be copied directly from an already-expanded observed edge

#### Scenario: Missing entrance geometry does not mutate the FSM
- **WHEN** current-frame reference context is `InnerTrace`
- **AND** `P_est` or the fixed-slope virtual boundary cannot produce a finite leading-contiguous segment
- **THEN** `CircleV2StepResult.reference_plan` SHALL be empty
- **AND** `CircleV2Reducer` state progression for that frame SHALL remain the authoritative `next_memory`
- **AND** geometry absence SHALL NOT reset, roll back, or skip a phase

#### Scenario: ExitTrace keeps existing outer straight edge behavior
- **WHEN** current-frame reference context is `ExitTrace` and direction is `left`
- **THEN** `CircleV2GeometryObserver` SHALL find the right-side straight edge and offset it leftward by `ordinary_road.half_width`
- **WHEN** direction is `right`
- **THEN** it SHALL find the left-side straight edge and offset it rightward by `ordinary_road.half_width`

### Requirement: Circle V2 Lifecycle And Parameters Are Explicit
The runtime SHALL expose `CIRCLE_V2_ENABLED`, `CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG`, `CIRCLE_V2_EXIT_HOLD_FRAMES`, `CIRCLE_V2_ENTRY_FIXED_SLOPE_LEFT_DX_DY`, and `CIRCLE_V2_ENTRY_FIXED_SLOPE_RIGHT_DX_DY`. `CircleV2Params` SHALL contain the exit yaw threshold, exit hold frames, and the two V3 entry fixed slopes. The yaw threshold SHALL NOT provide a dangerous zero business default.

`CIRCLE_V2_ENABLED` SHALL be owned by scene composition / scene registry, not by `CircleV2Reducer`.

#### Scenario: Entry fixed slopes are parsed and validated
- **WHEN** runtime parameters are loaded
- **THEN** `CIRCLE_V2_ENTRY_FIXED_SLOPE_LEFT_DX_DY` SHALL default to `-1.0`
- **AND** `CIRCLE_V2_ENTRY_FIXED_SLOPE_RIGHT_DX_DY` SHALL default to `1.0`
- **AND** `CIRCLE_V2_ENTRY_FIXED_SLOPE_LEFT_DX_DY` SHALL be accepted only as a finite negative value with absolute value no greater than `10.0`
- **AND** `CIRCLE_V2_ENTRY_FIXED_SLOPE_RIGHT_DX_DY` SHALL be accepted only as a finite positive value with absolute value no greater than `10.0`
- **AND** invalid values SHALL follow the existing runtime parameter parse-failure fallback behavior

#### Scenario: Entry fixed slopes use BEV dxdy coordinates
- **WHEN** `InnerTrace` constructs a V3 virtual boundary through `P_est`
- **THEN** the configured slope SHALL be interpreted as `dx/dy = lateral_m / forward_m`
- **AND** virtual boundary samples SHALL satisfy `x = P_est.x + slope_dx_dy * (y - P_est.y)`

#### Scenario: InnerTrace virtual boundary uses ordinary center forward samples
- **WHEN** `InnerTrace` constructs a V3 virtual boundary
- **THEN** the sampled `y` coordinates SHALL be taken from the leading finite `ordinary_road.center_path.sampled_path` forward coordinates
- **AND** sampling SHALL stop at the first absent or non-finite center sample
- **AND** no later sample after that stop point SHALL be used to pad or repair the leading segment
