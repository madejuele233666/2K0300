# Runtime Smoke Execution Evidence

## Build

- Linux command: `cd /home/madejuele/projects/2K0300/new/user && SKIP_UPLOAD=1 ./build.sh > /home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/build.log 2>&1`
- Result: build completed successfully (`build.log` refreshed on 2026-04-12).

## Controlled Failing Startup Path (Latest Attempt)

- Linux command: `cd /home/madejuele/projects/2K0300/new/user && VERIFY_LOG_PATH=/home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke-failing.log BOARD_BIN=/home/root/run_fail_imu_override.sh ./run_remote_smoke.sh; printf '%s\n' $? > /home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke-failing.exit`
- Local exit result: `1`
- Raw artifact status from `runtime-smoke-failing.log`:
  - `remote_upload_exit=1`
  - `remote_log_copy_exit=255`
  - `remote_log_copy_skipped=runtime_not_invoked`
  - `remote_runtime_exit=1`
  - `remote_runtime_invoked=0`
- Interpretation: on 2026-04-12 the board still rejected the upload path before runtime launch, so this run proved the helper preserved a failing local verdict but still did not produce copied remote runtime output.

## Direct-Match Startup Path (Latest Attempt)

- Linux command: `cd /home/madejuele/projects/2K0300/new/user && VERIFY_LOG_PATH=/home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke.log ./run_remote_smoke.sh; printf '%s\n' $? > /home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke.exit`
- Local exit result: `1`
- Raw artifact status from `runtime-smoke.log`:
  - `remote_upload_exit=1`
  - `remote_log_copy_exit=255`
  - `remote_log_copy_skipped=runtime_not_invoked`
  - `remote_runtime_exit=1`
  - `remote_runtime_invoked=0`
- Interpretation: on 2026-04-12 the board again failed before upload and launch completed, so no refreshed direct-match runtime diagnostics were captured in this attempt.

## Board Reachability During Latest Attempts

- Linux command: `ping -c 2 10.236.192.226`
- Result: 100% packet loss on 2026-04-12.
- Linux command: `ssh -o BatchMode=yes -o ConnectTimeout=5 root@10.236.192.226 'echo ok'`
- Result: `Connection closed by 10.236.192.226 port 22` on 2026-04-12.
- Conclusion: current verification blocker is board reachability or board-side SSH acceptance. Runtime smoke evidence requiring successful upload, launch, copied remote log, and refreshed hardware-discovery output remains pending.

## Current Reachable Board Address

- Serial-console inspection on 2026-04-12 showed the board is currently reachable on `wlan0=10.100.170.226/24`, while `eth0` remains down and fails to initialize.
- Linux command: `ssh root@10.100.170.226 'echo ok'`
- Result: SSH to `10.100.170.226` succeeded after starting the board-side `dropbear` service and refreshing the local host key.

## Skip-IMU Runtime Smoke (User-Directed Workaround)

- Purpose: continue board validation while IMU hardware discovery is known-broken.
- Board wrapper: `/home/root/run_skip_imu_profile.sh`
- Alternate profile: `openspec/changes/fix-board-runtime-hardware-detection/verification/hardware_profile.skip-imu.json`
- Linux command: `cd /home/madejuele/projects/2K0300/new/user && BOARD_IP=10.100.170.226 VERIFY_LOG_PATH=/home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke-skip-imu.log BOARD_BIN=/home/root/run_skip_imu_profile.sh ./run_remote_smoke.sh; printf '%s\n' $? > /home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke-skip-imu.exit`
- Local exit result: `1`
- Key result:
  - startup advanced past camera and disabled IMU handling
  - startup then failed at encoder initialization with `encoder resource unavailable: /dev/zf_encoder_1`
  - helper preserved truthful remote verdict with `remote_runtime_exit=1`, `remote_log_copy_exit=0`, and `remote_runtime_invoked=1`
- Evidence:
  - `runtime-smoke-skip-imu.log`
  - `runtime-smoke-skip-imu.exit`
  - `hardware-discovery-skip-imu.log`

## Skip-IMU-And-Encoder Runtime Smoke (User-Directed Workaround)

- Purpose: continue past the IMU and encoder blockers to exercise remaining startup and control-loop paths.
- Board wrapper: `/home/root/run_skip_imu_encoder_profile.sh`
- Alternate profile: `openspec/changes/fix-board-runtime-hardware-detection/verification/hardware_profile.skip-imu-encoder.json`
- Linux command: `cd /home/madejuele/projects/2K0300/new/user && BOARD_IP=10.100.170.226 VERIFY_LOG_PATH=/home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke-skip-imu-encoder.log BOARD_BIN=/home/root/run_skip_imu_encoder_profile.sh ./run_remote_smoke.sh; printf '%s\n' $? > /home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke-skip-imu-encoder.exit`
- Local exit result: `0`
- Key result:
  - startup completed with camera direct-match, ADC low-voltage checks, motor direct-match initialization, and PIT timer startup
  - runtime reached `LS2K_MAX_FRAMES=400` and exited cleanly
  - control-loop remained vetoed because IMU and encoder were intentionally disabled in degraded mode
  - first emergency-stop path exposed a remaining motor write failure: `motor PWM write failed: /dev/zf_device_pwm_motor_1`
- Evidence:
  - `runtime-smoke-skip-imu-encoder.log`
  - `runtime-smoke-skip-imu-encoder.exit`

## Post-Fix Build And Deploy

- Bridge fix scope:
  - `new/code/platform/true_ls2k0300/encoder_bridge.cpp`
  - `new/code/platform/true_ls2k0300/motor_bridge.cpp`
- Fix rationale:
  - the board encoder and PWM drivers return `0` on successful `read()` / `write()`, so the previous byte-count equality checks falsely treated successful I/O as failures
  - the encoder driver copies a 4-byte integer to userspace, so the bridge now reads into `int32_t` before narrowing to `int16_t`
- Binary consistency check on 2026-04-12:
  - local `md5sum new/out/new` = `f15b63f80cba6d8b93b8fa99895e126d`
  - remote `ssh root@10.100.170.226 'md5sum /home/root/new'` = `f15b63f80cba6d8b93b8fa99895e126d`

## Post-Fix Skip-IMU Runtime Smoke

- Purpose: rerun the previously failing encoder path after the bridge repair while keeping the known-bad IMU path disabled per user instruction.
- Linux command: `cd /home/madejuele/projects/2K0300/new/user && BOARD_IP=10.100.170.226 VERIFY_LOG_PATH=/home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke-skip-imu-after-fix.log BOARD_BIN=/home/root/run_skip_imu_profile.sh ./run_remote_smoke.sh; printf '%s\n' $? > /home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke-skip-imu-after-fix.exit`
- Local exit result: `0`
- Key result:
  - startup now reaches camera, ADC, encoder, motor, and timer initialization successfully
  - encoder initialization now reports direct-match resources `left=/dev/zf_encoder_1` and `right=/dev/zf_encoder_2`
  - runtime reaches `LS2K_MAX_FRAMES=400` and exits cleanly
  - control output remains fail-safe vetoed because IMU is intentionally disabled in degraded mode
- Raw artifact status from `runtime-smoke-skip-imu-after-fix.log`:
  - `remote_runtime_exit=0`
  - `remote_log_copy_exit=0`
  - `remote_runtime_invoked=1`
- Evidence:
  - `runtime-smoke-skip-imu-after-fix.log`
  - `runtime-smoke-skip-imu-after-fix.exit`

## Post-Fix Skip-IMU-And-Encoder Runtime Smoke

- Purpose: rerun the previously motor-write-failing degraded path after the motor bridge repair.
- Linux command: `cd /home/madejuele/projects/2K0300/new/user && BOARD_IP=10.100.170.226 VERIFY_LOG_PATH=/home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke-skip-imu-encoder-after-fix.log BOARD_BIN=/home/root/run_skip_imu_encoder_profile.sh ./run_remote_smoke.sh; printf '%s\n' $? > /home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke-skip-imu-encoder-after-fix.exit`
- Local exit result: `0`
- Key result:
  - startup again confirms camera, ADC, and motor direct-match resources
  - the earlier `motor PWM write failed: /dev/zf_device_pwm_motor_1` failure no longer appears
  - runtime reaches `LS2K_MAX_FRAMES=400`, exits cleanly, and performs clean shutdown
  - control output remains fail-safe vetoed because IMU and encoder are intentionally disabled in degraded mode
- Raw artifact status from `runtime-smoke-skip-imu-encoder-after-fix.log`:
  - `remote_runtime_exit=0`
  - `remote_log_copy_exit=0`
  - `remote_runtime_invoked=1`
- Evidence:
  - `runtime-smoke-skip-imu-encoder-after-fix.log`
  - `runtime-smoke-skip-imu-encoder-after-fix.exit`

## Last Known Hardware Discovery Snapshot (Earlier Successful Session)

- Evidence file: `/home/madejuele/projects/2K0300/openspec/changes/fix-board-runtime-hardware-detection/verification/hardware-discovery.log`
- Snapshot values:
  - `/sys/bus/iio/devices/iio:device0/name=1611c000.adc`
  - SPI IMU device present with `modalias=spi:imu`
  - SPI IMU device compatible string `seekfree,imu`

## Serial-Captured Direct-Match Runtime Evidence (Accepted For Closure)

- Evidence refresh time: 2026-04-12 17:23:09 to 17:23:20 +08:00
- Acceptance note: at archive time on 2026-04-15, the board's SSH address was no longer reachable at either prior known IP (`10.236.192.226`, `10.100.170.226`), and live `/dev/ttyS*` access from this WSL session returned `I/O error` even under `sudo`. The archive decision therefore relied on these 2026-04-12 serial-captured direct-match logs as the best available intended-profile board evidence. This note is preserved as historical closure context; the later 2026-04-15 post-archive retry below supersedes it as the newest direct-match runtime proof.
- Runtime smoke evidence:
  - `serial-runtime-smoke.log`
  - `serial-runtime-smoke-failing.log`
  - `serial-hardware-discovery.log`
- Direct-match startup result from `serial-runtime-smoke.log`:
  - camera path resolved as `/dev/video0`
  - ADC low-voltage path resolved as `/sys/bus/iio/devices/iio:device0/in_voltage7_raw`
  - IMU direct-match startup failed closed with `imu unavailable: no supported IMU name file found under /sys/bus/iio/devices`
  - IMU diagnostics recorded `imu detection path selected: unknown source=unresolved`
  - startup remained fail-safe and refused to arm actuators
- Hardware inventory from `serial-hardware-discovery.log`:
  - `/sys/bus/iio/devices/iio:device0/name=1611c000.adc`
  - runtime resources include `/dev/video0`
  - runtime resources include `/dev/zf_device_pwm_motor_1` and `/dev/zf_device_pwm_motor_2`
  - runtime resources include `/dev/zf_driver_gpio_motor_1` and `/dev/zf_driver_gpio_motor_2`
  - runtime resources include `/dev/zf_encoder_1` and `/dev/zf_encoder_2`
- Closure rationale:
  - this serial-captured direct-match run is the missing intended-profile evidence called out by the 2026-04-12 blocked implementation review
  - together, the serial runtime log and serial hardware-discovery snapshot now name the IMU discovery failure, camera path, ADC path, and encoder/motor runtime resources needed to explain the fail-safe startup outcome
- Result: task 3.2 was satisfied for archive by the refreshed serial direct-match evidence bundle even though SSH IP churn prevented a new remote-copy run at that moment; the later post-archive retry then added a stronger successful remote-copy proof on the refreshed WLAN address.

## Windows Command Resolution

- No Windows runner was used in this verification session.

## 2026-04-15 Post-Archive Retry Via WSL USB Attach

- Trigger: after archive, the host-side serial device was reattached into WSL with `usbipd`, which restored direct console access and exposed the board's new WLAN address.
- Windows-side attach command: `usbipd attach --wsl --busid 5-2`
- Attached device: `SEEKFREE Wireless Com Port (COM7)` -> `/dev/ttyACM0`
- Serial-console command result:
  - `ip -4 addr show`
  - active board address resolved to `wlan0=192.168.255.226/24`
- SSH status after retry:
  - `ssh root@192.168.255.226 'hostname; ps | grep dropbear'`
  - board accepted SSH on the new WLAN address and reported active `dropbear`
- Linux command: `cd /home/madejuele/projects/2K0300/new/user && BOARD_IP=192.168.255.226 VERIFY_LOG_PATH=/home/madejuele/projects/2K0300/openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.log ./run_remote_smoke.sh; printf '%s\n' $? > /home/madejuele/projects/2K0300/openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.exit`
- Local exit result: `0`
- Runtime evidence files:
  - `runtime-smoke-retry-2026-04-15.log`
  - `runtime-smoke-retry-2026-04-15.exit`
  - `hardware-discovery-retry-2026-04-15.log`
- Key results from the retry run:
  - camera path resolved as `/dev/video0`
  - ADC low-voltage path resolved as `/sys/bus/iio/devices/iio:device0/in_voltage7_raw`
  - IMU initialized successfully through direct-match detection with resolved resource `/sys/bus/iio/devices/iio:device1`
  - IMU diagnostics named `imu660ra source=/sys/bus/iio/devices/iio:device1/name`
  - encoder initialized successfully with `left=/dev/zf_encoder_1` and `right=/dev/zf_encoder_2`
  - motor initialized successfully with PWM and GPIO resources under `/dev/zf_device_pwm_motor_*` and `/dev/zf_driver_gpio_motor_*`
  - startup reached `startup.complete`, timer start, control-loop start, bounded frame exit, and clean shutdown
  - `remote_runtime_exit=0`, `remote_log_copy_exit=0`, `remote_runtime_invoked=1`
- Interpretation:
  - the earlier archive closure used the best available evidence at the time, including serial direct-match logs
  - the post-archive retry on the refreshed WLAN address confirms the intended default direct-match profile now completes successfully on-board, rather than only failing closed with diagnosable IMU discovery output
