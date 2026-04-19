# B-1 Static Safety And Lifecycle Boundary

## Command

Diagnostics-only wrapper smoke:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b1-static-safety.log \
  SMOKE_ENABLE_MOTOR=0 \
  SMOKE_AUTO_START=0 \
  SMOKE_MAX_FRAMES=20 \
  ./run_remote_smoke.sh)
```

Baseline product-owned lifecycle path:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  LS2K_ALLOW_DEGRADED_STARTUP=1 \
  LS2K_PROFILE_PATH=../config/hardware_profile.json \
  LS2K_PARAMS_PATH=../config/default_params.json \
  ../out/new > ../verification/phase-b-b1-manual-lifecycle.log 2>&1 & \
  RUNTIME_PID=$! && \
  sleep 1 && kill -USR2 "${RUNTIME_PID}" && \
  sleep 1 && kill -INT "${RUNTIME_PID}" && \
  wait "${RUNTIME_PID}")
```

## Expected Markers

- `main.harness_context`
- `main.frame.processed`
- `startup.complete`
- `control.start`
- `motion.start.requested`
- `motion.phase.transition`
- `motion.stop.requested`
- `motion.stop.complete`
- `main.exit.ready`
- `shutdown.complete`

## Harness Context

The wrapper log must contain a dedicated `harness_context_begin ... harness_context_end` block showing the injected `LS2K_AUTO_*`, `SMOKE_MAX_FRAMES`, and fault-injection env values. When bounded frame budget is enabled, the wrapper also relies on `main.frame.processed` and then sends `SIGINT` so the runtime reaches `DISARMED` before exit; timeout recovery may escalate to `SIGTERM` / `SIGKILL` if a faulted session cannot terminate gracefully. The direct product-owned command above intentionally does not use that harness block; its evidence is the signal-driven lifecycle markers in the runtime log.

## Proves

- Product runtime distinguishes start intent, controlled stop, and process exit.
- Diagnostics-only smoke no longer implies real drive permission.
- The accepted runtime entrypoint exposes a non-test manual lifecycle path.

## Does Not Prove

- Real wheel direction or low-speed ground behavior.
- Sensor-quality or closed-loop tuning quality.
- Fault-latch recovery under injected failures.

## Result

Status: `completed` on `2026-04-19`

Accepted evidence:

- `new/verification/phase-b-b1-static-safety-rerun.log`
- `new/verification/phase-b-b1-manual-lifecycle.log`

## Observed Outcome

Diagnostics-only wrapper rerun:

- `phase-b-b1-static-safety-rerun.log` completed with `remote_runtime_exit=0`.
- Observed markers include `main.harness_context`, repeated `main.frame.processed`, `startup.complete`, `control.start`, `motion.stop.requested`, `main.exit.ready`, and `shutdown.complete`.
- This is the accepted `B-1` wrapper-owned bounded-stop evidence for the current tree.

Manual signal-driven lifecycle run:

- `phase-b-b1-manual-lifecycle.log` showed successful startup on the accepted board path, then `motion.start.requested` from `SIGUSR2`, followed by `DISARMED -> START_REQUESTED -> SPINUP`.
- The same run then entered `FAIL_SAFE_LATCHED` on `perception_stale` before a clean `motion.stop.complete`, but it still recorded `motion.stop.requested` and finished with `shutdown.complete` after operator stop / teardown.
- For `B-1`, this is accepted as evidence that the product-owned signal boundary exists and that the runtime preserves fail-safe-first behavior under an interrupted early manual start. It is not reused as `B-2+` motion-quality evidence.

## Closure Note

`B-1` is closed as a static safety and lifecycle-boundary checkpoint, not as a low-speed motion-quality checkpoint. The remaining motion-quality work stays in `B-2` through `B-5`.
