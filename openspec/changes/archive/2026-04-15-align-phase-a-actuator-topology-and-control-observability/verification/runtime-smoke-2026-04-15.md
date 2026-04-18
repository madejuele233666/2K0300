# Runtime Smoke Rerun (2026-04-15)

- Change: `align-phase-a-actuator-topology-and-control-observability`
- Board: `root@192.168.255.226`
- Command:
  - `cd /home/madejuele/projects/2K0300/new/user && BOARD_IP=192.168.255.226 ./build.sh`
  - `cd /home/madejuele/projects/2K0300/new/user && BOARD_IP=192.168.255.226 VERIFY_LOG_PATH=/home/madejuele/projects/2K0300/openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/runtime-smoke-2026-04-15.log ./run_remote_smoke.sh; printf '%s\n' $? > /home/madejuele/projects/2K0300/openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/runtime-smoke-2026-04-15.exit`
- Exit result:
  - `runtime-smoke-2026-04-15.exit` = `0`

## Connection To Accepted Baseline

This rerun preserves the same direct-match startup shape already accepted in:

- `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.log`
- `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.exit`
- `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/hardware-discovery-retry-2026-04-15.log`

The rerun again reached:

- `imu.init`
- `imu.detect`
- `startup.complete`
- `control.start`
- `shutdown.complete`

So the accepted baseline is still intact after the actuator-contract cleanup and runtime observability refactor.

## New Observability Markers Confirmed

The rerun also exercised the new project-owned control observability markers introduced by this change:

- initial fail-safe gate:
  - `control.veto`
  - `control.veto.perception_stale`
  - `control.apply.emergency_stop`
- successful non-empty control application:
  - `control.command.requested_nonzero`
  - `control.apply.drive`
  - `control.arm.transition`

## Interpretation

- The first control cycle still fails safe for an explicit reason: stale perception.
- Once perception is fresh enough, the runtime requests non-zero differential-drive output and records a successful motor apply.
- The transition from disarmed to armed is now visible in the same board log instead of being inferred indirectly.

## Evidence Files

- `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/runtime-smoke-2026-04-15.log`
- `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/runtime-smoke-2026-04-15.exit`
