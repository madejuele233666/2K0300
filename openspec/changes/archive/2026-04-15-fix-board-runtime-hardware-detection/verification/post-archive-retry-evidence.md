# Post-Archive Retry Evidence

## Scope

This note preserves the successful 2026-04-15 post-archive retry that reattached
the board serial device into WSL, rediscovered the board's new WLAN address,
and reran the default direct-match smoke path successfully.

## Acquisition Path

- Windows host attach command: `usbipd attach --wsl --busid 5-2`
- Attached device identity: `SEEKFREE Wireless Com Port (COM7)`
- WSL serial node after attach: `/dev/ttyACM0`
- Serial-console network check result: `wlan0=192.168.255.226/24`

## Retry Commands

- Serial-console command:
  - `ip -4 addr show`
- Remote smoke command:
  - `cd /home/madejuele/projects/2K0300/new/user && BOARD_IP=192.168.255.226 VERIFY_LOG_PATH=/home/madejuele/projects/2K0300/openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.log ./run_remote_smoke.sh; printf '%s\n' $? > /home/madejuele/projects/2K0300/openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.exit`
- Hardware snapshot command:
  - `ssh root@192.168.255.226 '<hardware discovery commands>' > /home/madejuele/projects/2K0300/openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/hardware-discovery-retry-2026-04-15.log`

## Saved Artifacts

- [runtime-smoke-retry-2026-04-15.log](/home/madejuele/projects/2K0300/openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.log)
- [runtime-smoke-retry-2026-04-15.exit](/home/madejuele/projects/2K0300/openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.exit)
- [hardware-discovery-retry-2026-04-15.log](/home/madejuele/projects/2K0300/openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/hardware-discovery-retry-2026-04-15.log)
- [runtime-smoke-execution-evidence.md](/home/madejuele/projects/2K0300/openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-execution-evidence.md)

## Key Results

- Local smoke exit: `0`
- `remote_runtime_exit=0`
- `remote_log_copy_exit=0`
- `remote_runtime_invoked=1`
- Camera path: `/dev/video0`
- ADC path: `/sys/bus/iio/devices/iio:device0/in_voltage7_raw`
- IMU resolved successfully at `/sys/bus/iio/devices/iio:device1`
- IMU detection result: `imu660ra source=/sys/bus/iio/devices/iio:device1/name`
- Encoder resources: `/dev/zf_encoder_1`, `/dev/zf_encoder_2`
- Motor resources:
  - `/dev/zf_device_pwm_motor_1`
  - `/dev/zf_device_pwm_motor_2`
  - `/dev/zf_driver_gpio_motor_1`
  - `/dev/zf_driver_gpio_motor_2`
- Runtime reached bounded frame exit and clean shutdown.

## Interpretation

The archived change had already been closed using the best available evidence at
the time. This retry strengthens the archived record by confirming that the
default direct-match profile now completes successfully on-board at the refreshed
WLAN address, rather than only failing closed with diagnosable startup output.
