# B-5 Fault Injection And Recovery

## Commands

Dropped frame:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b5-drop-frame.log \
  SMOKE_ENABLE_MOTOR=1 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_AUTO_RESET_FAULT=1 \
  SMOKE_FAULT_INJECT_DROP_FRAME_EVERY_N=5 \
  SMOKE_MAX_FRAMES=120 \
  ./run_remote_smoke.sh)
```

Invalid IMU:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b5-imu-invalid.log \
  SMOKE_ENABLE_MOTOR=1 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_AUTO_RESET_FAULT=1 \
  SMOKE_FAULT_INJECT_IMU_INVALID_EVERY_N=10 \
  SMOKE_MAX_FRAMES=120 \
  ./run_remote_smoke.sh)
```

Invalid encoder:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b5-encoder-invalid.log \
  SMOKE_ENABLE_MOTOR=1 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_AUTO_RESET_FAULT=1 \
  SMOKE_FAULT_INJECT_ENCODER_INVALID_EVERY_N=7 \
  SMOKE_MAX_FRAMES=120 \
  ./run_remote_smoke.sh)
```

Forced low voltage:

```bash
(cd new/user && \
  mkdir -p ../verification && \
  VERIFY_LOG_PATH=../verification/phase-b-b5-low-voltage.log \
  SMOKE_ENABLE_MOTOR=1 \
  SMOKE_AUTO_START=1 \
  SMOKE_AUTO_START_DELAY_MS=200 \
  SMOKE_AUTO_RESET_FAULT=1 \
  SMOKE_FORCE_LOW_VOLTAGE=true \
  SMOKE_MAX_FRAMES=120 \
  ./run_remote_smoke.sh)
```

## Expected Markers

- `main.harness_context`
- `main.frame.processed`
- `perception.inject.drop_frame` or `imu.inject.invalid` or `encoder.inject.invalid` or `power.low_voltage.injected`
- `control.veto.*`
- `motion.failsafe.latched`
- `motion.failsafe.reset_ready`
- `motion.failsafe.reset_requested`
- `motion.failsafe.rearmed`

## Harness Context

The wrapper header is authoritative for which automation and fault env values were injected. When `SMOKE_MAX_FRAMES` is non-zero, pair that header with `main.frame.processed` in the runtime log so the bounded stop remains wrapper-owned even during fault drills. Do not rely on runtime markers alone to infer the test setup.

## Proves

- Fault injection still runs through the accepted product runtime entrypoint.
- Runtime faults drive `FAIL_SAFE_LATCHED` instead of silent zero-output ambiguity.
- Re-arm remains explicit; test automation only synthesizes the reset after the latch-clear and hold boundary.

## Does Not Prove

- Real hardware faults beyond the injected path.
- Long-duration recovery stability after repeated faults.
- Phase C or higher scenario behavior.
