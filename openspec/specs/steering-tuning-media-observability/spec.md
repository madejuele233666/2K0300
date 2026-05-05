# steering-tuning-media-observability Specification

## Purpose
Define the project-owned steering-tuning observability surface, including the runtime steering snapshot, the separate steering media sidecar, and the preserved evidence bundle used for review.

## Requirements
### Requirement: Runtime Steering Snapshot Exposes The Current Reference/Control Chain
The runtime SHALL expose a project-owned steering tuning snapshot that can explain one control cycle from perception health through selected reference facts, reference usability, curvature, reference-control readiness, safety gate, yaw target, and final actuator output. At minimum, the accepted snapshot SHALL include these grouped objects:

- `perception_health`
- `reference`
- `eligibility`
- `curvature`
- `reference_control`
- `safety_gate`
- `degraded`
- `yaw_control`
- `actuator`
- `element_evidence`

#### Scenario: Steering-chain evidence is visible without assistant rendering
- **WHEN** reviewers inspect project-owned diagnostics, structured export, or harness-visible evidence during a steering tuning run
- **THEN** they SHALL be able to identify the accepted steering snapshot groups above from a project-owned non-assistant evidence surface such as `control.steering_snapshot`
- **AND** assistant connectivity or vendor-only image rendering SHALL NOT be required to explain the steering chain

### Requirement: Steering Media Uses A Separate Read-Only Board-To-Host Session
The accepted steering tuning observability path SHALL use a project-owned steering media TCP session that is separate from the accepted assistant JSON control session. The board SHALL actively connect to the host for this media session, and the media session SHALL remain read-only push traffic.

#### Scenario: Media session remains separate from the command-bearing control session
- **WHEN** the accepted steering media path is configured
- **THEN** the board SHALL connect to the same configured host or equivalent accepted host target using a dedicated steering media port
- **AND** the steering media session SHALL NOT accept commands, ACKs, or state mutations

### Requirement: Steering Media Endpoint Uses A Frozen Board-Side Configuration Surface
The accepted steering media endpoint SHALL use a project-owned startup-loaded configuration surface rather than hardcoded transport constants or ad hoc setup. For the accepted first release, the board SHALL source the steering media host from `assistant_tcp.host` and SHALL source steering media-specific wiring from:

- `steering_media_enabled`
- `steering_media_port`
- `steering_media_publish_interval_ms`

#### Scenario: Accepted configuration shape is concrete and reviewable
- **WHEN** implementers wire the accepted steering media path on the board
- **THEN** they SHALL configure it through the frozen project-owned parameter keys above plus the existing `assistant_tcp.host`
- **AND** they SHALL NOT need an undocumented second host field or manual hardcoded transport override for accepted behavior

### Requirement: Steering Media Protocol Uses One Length-Prefixed Binary Envelope
The accepted first-release steering media protocol SHALL use one length-prefixed binary envelope carrying a UTF-8 JSON header plus an optional raw payload. The accepted first-release frame families SHALL be:

- `config_snapshot`
- `image_frame`

The accepted first-release `config_snapshot` SHALL be sent once after the media session becomes ready. Its JSON header SHALL use this exact top-level object shape:

- `type="config_snapshot"`
- `publish_time_ms`
- `media_publish_interval_ms`
- `param_snapshot`

The accepted `config_snapshot.param_snapshot` object SHALL include the current runtime parameter groups needed to interpret steering media evidence:

- `running_speed_target`
- `yaw_rate_pid`
- `control_period_ms`
- `low_voltage_sample_interval_ms`
- `low_voltage_raw_threshold`
- `raw_turn_output_limit`
- `BEV_PROJECTOR`
- `BEV_GEOMETRY`
- `BEV_CLASSIFICATION`
- `BEV_CONTROL_MODEL`
- `BEV_ELEMENT`

The accepted `image_frame` SHALL carry a raw grayscale payload whose dimensions are declared in the same frame header. Its JSON header SHALL use this top-level object shape:

- `type="image_frame"`
- `frame_id`
- `capture_time_ms`
- `publish_time_ms`
- `motion_phase`
- `pixel_format="gray8"`
- `width`
- `height`
- `steering_snapshot`

The accepted `image_frame.steering_snapshot` object SHALL use the same grouped steering snapshot contract as `control.steering_snapshot`.

#### Scenario: First-release host tooling can parse the media contract deterministically
- **WHEN** implementers build the board-side steering media publisher and the accepted host recorder
- **THEN** they SHALL be able to interoperate through one documented length-prefixed binary envelope with `config_snapshot` and `image_frame`
- **AND** the host SHALL NOT need a second undocumented framing model to recover image payload size or steering metadata

#### Scenario: Image payload size is declared and validated
- **WHEN** the runtime publishes an accepted `image_frame`
- **THEN** the payload SHALL contain exactly `width * height` grayscale bytes
- **AND** the header and envelope lengths SHALL be sufficient for the host to validate that payload deterministically

### Requirement: Steering Media Publication Is Non-Blocking And Drop-Tolerant
The steering media path SHALL be strictly lower priority than the accepted control plane. When the media socket is busy or unavailable, the runtime SHALL drop old media frames instead of blocking, back-pressuring, or degrading the accepted command/ACK/state path.

#### Scenario: Busy media transport drops stale frames without harming control behavior
- **WHEN** the steering media socket cannot accept a frame immediately
- **THEN** the runtime SHALL drop or replace stale media frames according to a latest-frame policy
- **AND** ACK generation, state publication, telemetry publication, disconnect clear behavior, and fail-safe rejection behavior on the accepted control plane SHALL remain unaffected

### Requirement: Accepted Host Evidence Bundle Covers Steering Media And Control Alignment
The accepted steering tuning workflow SHALL preserve a minimal project-owned evidence bundle that can correlate steering media and control-plane behavior from the same run.

#### Scenario: Steering tuning evidence bundle is complete enough for review
- **WHEN** the accepted workflow records a steering tuning session
- **THEN** the preserved evidence SHALL include at minimum a board log, host control CSV, steering media records, and a time-aligned summary or equivalent alignment metadata
- **AND** reviewers SHALL be able to explain a steering event without depending on undocumented manual alignment steps

### Requirement: Public Steering Snapshots Expose Element Evidence Facts
The project-owned public steering snapshot SHALL include a read-only `element_evidence` group. The first accepted element evidence group SHALL be `cross_exit`, and it SHALL expose detector facts plus candidate summary status without becoming a control authority.

#### Scenario: Steering snapshot includes cross-exit evidence facts
- **WHEN** reviewers inspect `control.steering_snapshot`
- **THEN** they SHALL find `element_evidence.cross_exit` fields for presence, confidence, metric bounds, support counts, and reason
- **AND** they SHALL find `element_evidence.cross_exit.candidate` fields for `built`, `takeover_enabled`, `included_in_arbitration`, and `reason`
- **AND** they SHALL NOT need archive-only topology, scene, or policy fields to explain the evidence

### Requirement: Steering Media Mirrors Element Evidence In Image Headers
The steering media `image_frame.steering_snapshot` object SHALL mirror the public steering snapshot `element_evidence` group so image evidence and control evidence can be aligned from the same media bundle.

#### Scenario: Image frame header carries cross-exit evidence
- **WHEN** the runtime publishes an accepted steering media `image_frame`
- **THEN** the JSON header SHALL include `steering_snapshot.element_evidence.cross_exit`
- **AND** the mirrored `cross_exit.candidate` object SHALL include `built`, `takeover_enabled`, `included_in_arbitration`, and `reason`
- **AND** the raw grayscale payload framing and read-only media session semantics SHALL remain unchanged

### Requirement: Steering Media Config Snapshot Includes Element Parameters
The steering media `config_snapshot.param_snapshot` object SHALL include `BEV_ELEMENT` so element evidence and candidate inclusion behavior can be interpreted from a captured media bundle.

#### Scenario: Config snapshot carries cross-exit takeover state
- **WHEN** the runtime publishes a steering media `config_snapshot`
- **THEN** the JSON header SHALL include `param_snapshot.BEV_ELEMENT.CROSS_EXIT_TAKEOVER_ENABLED`
- **AND** the field SHALL reflect the startup-loaded runtime parameter value
