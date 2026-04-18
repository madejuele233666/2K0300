## ADDED Requirements

### Requirement: Direct-Match Hardware Discovery Is Truthful
Direct-match bridge and adapter code SHALL verify the concrete board resource it intends to use before it reports a subsystem as ready.

#### Scenario: IMU discovery resolves a real board node
- **WHEN** the direct-match IMU path initializes on a board where `/sys/bus/iio/devices/` numbering differs from previous assumptions
- **THEN** the platform-owned bridge SHALL enumerate or otherwise resolve the active IMU node from real board-visible evidence, and it SHALL NOT hard-code `iio:device1` as an unverified invariant

#### Scenario: Missing direct-match resource blocks readiness
- **WHEN** a direct-match device node or sysfs resource required by IMU, encoder, motor, or ADC logic cannot be resolved
- **THEN** the owning adapter SHALL report not-ready/fail-safe with diagnosable evidence naming the unresolved resource, rather than claiming successful initialization

### Requirement: Bridge I/O Failures Are Propagated
Platform-owned bridges SHALL surface read and write failures to adapters instead of fabricating valid samples, zero values, or successful actuator writes.

#### Scenario: Encoder read failure is not reported as valid data
- **WHEN** an encoder count read fails because the device node is missing, unreadable, or returns malformed data
- **THEN** the bridge SHALL return an invalid result, and the adapter/runtime SHALL preserve fail-safe veto behavior instead of publishing a valid delta sample

#### Scenario: Actuator write failure is not reported as success
- **WHEN** PWM or GPIO writes fail during motor initialization, command application, or shutdown
- **THEN** the motor path SHALL surface that failure through diagnostics and readiness/apply status so actuator state is not falsely reported as available

#### Scenario: ADC acquisition failure stays distinguishable from low voltage
- **WHEN** the battery ADC path cannot be read
- **THEN** the power-monitor path SHALL report the sample as unavailable rather than converting the failure into a synthetic raw value that looks like a real low-voltage reading

## MODIFIED Requirements

### Requirement: True-Library Stack Equivalents Are Named Per Critical Device
Each critical device contract from the TC264-side port SHALL map to an explicit true-baseline construct, bridge file, or documented non-support decision, and each direct-match mapping SHALL name how the concrete board resource is resolved.

#### Scenario: Critical mappings are reviewable
- **WHEN** design and implementation are reviewed
- **THEN** camera, IMU, encoder, timer, PWM/GPIO motor control, ADC-based low-voltage checks, diagnostics, and parameter persistence SHALL each have a named true-baseline-side construct or implementation file, SHALL document whether the resource is fixed, enumerated, or override-driven, and unsupported contracts SHALL name the adaptation boundary instead of being silently deferred

### Requirement: Fail-Safe Runtime Behavior Survives The Retarget
The retarget SHALL continue to veto or clamp actuator output when perception, sensor, parameter, or baseline-normalization state is invalid, including cases where bridge I/O fails or direct-match resource discovery cannot be trusted.

#### Scenario: Invalid normalized state suppresses actuation
- **WHEN** camera snapshots are empty, IMU normalization fails, encoder delta state is not yet trustworthy, a bridge read/write fails, direct-match hardware discovery cannot resolve the intended board resource, or parameter application violates a startup-critical requirement
- **THEN** the control path SHALL suppress actuator updates and record the reason through diagnostics instead of continuing with stale, fabricated, vendor-global, or partially normalized data
