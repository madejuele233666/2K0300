# B-2 Half-Load Spinup And Stop

## Command

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b2-half-load.log \
  SMOKE_ENABLE_MOTOR=1 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_MAX_FRAMES=120 \
  ./run_remote_smoke.sh)
```

## Expected Markers

- `main.harness_context`
- `main.frame.processed`
- `motion.start.requested`
- `motion.phase.transition`
- `motion.spinup.enter`
- `motion.spinup.complete`
- `control.apply.drive`
- `control.apply.command`
- `motion.stop.requested`
- `motion.stop.complete`

## Harness Context

Record the wrapper header plus any PWM-safety controls used on the board side. If `SMOKE_MAX_FRAMES` is non-zero, confirm the log also contains `main.frame.processed` so the wrapper-owned budget can trigger `SIGINT` rather than leaving frame-stop semantics inside the product runtime. If extra motor-limiting hardware settings or battery limits are in effect, note them adjacent to the log path.

## Proves

- The runtime reaches `SPINUP` and `RUNNING` through the new project-owned lifecycle.
- Startup and stop use shaped motion phases instead of an immediate process-lifetime jump.
- Half-load evidence can correlate lifecycle phases with actuator commands.

## Does Not Prove

- Ground-contact stability.
- Straight-line tracking quality.
- Turn correction quality.

## Result

Status: `completed` on `2026-04-19`

Accepted evidence:

- `new/verification/phase-b-b2-half-load-short-pwm3000.log`

Accepted run shape:

- `SMOKE_ENABLE_MOTOR=1`
- `SMOKE_AUTO_START=1`
- `SMOKE_AUTO_START_DELAY_MS=200`
- `SMOKE_MAX_FRAMES=80`
- `pwm_limit=3000`

## Observed Outcome

- The board run used the real motor path: `profile.motor=direct-match:pwm-gpio-free-function`.
- The lifecycle progressed through `motion.start.requested`, `DISARMED -> START_REQUESTED -> SPINUP -> RUNNING -> STOPPING -> DISARMED`.
- The run recorded `motion.spinup.enter`, `motion.spinup.complete`, `control.apply.drive`, `control.apply.command`, `motion.stop.requested`, `motion.stop.complete`, and `main.exit.ready`.
- The highest observed command in this accepted short run was `left_pwm=760 right_pwm=0`, well below the current `pwm_limit=3000`.

## Closure Note

`B-2` is accepted as a short half-load board pass under the current conservative PWM limit. This closes the lifecycle-oriented half-load checkpoint for the present board-testing phase, but it does not replace later `B-3` / `B-4` ground-contact motion evidence.
