# Phase D Assistant Speed Tuning

## Purpose

This note fixes the accepted board-plus-host workflow for live speed tuning over the bidirectional assistant TCP sidecar.

The accepted topology is:

1. board runtime acts as TCP client
2. host tuning tool acts as TCP listener
3. one newline-delimited JSON session carries `command`, `ack`, `state`, and `telemetry`
4. one separate steering media TCP session carries length-prefixed `config_snapshot` and `image_frame`
5. optional assistant waveform/image publication remains support-only evidence

## Accepted Commands

Allowed first-release commands:

- `enable_tuning_mode`
- `disable_tuning_mode`
- `start`
- `stop`
- `set_turn_suppressed`
- `set_target_speed`

Rejected first-release shortcuts:

- writing PID gains online
- treating `stop` as snapshot clear
- bypassing `motion_intent`
- adding a second ad hoc host protocol

## Accepted Host Entry Point

Use the project-owned wrapper instead of ad hoc socket scripts:

```bash
(cd new/user && ./debug.sh tuning \
  --sequence 20,40,60,77 \
  --ttl-ms 2500 \
  --step-dwell-ms 1200 \
  --disabled-mode-checks \
  --invalid-target-speed 170 \
  --media-listen-port 8890 \
  --csv ../verification/phase-d-speed-tuning.csv)
```

The accepted host tool defaults keep transport timing explicit:

- `startup_delay_ms=0`
- `command_gap_ms=0`

Add settle gaps explicitly only when the current board/runtime path needs them for transport debugging or staged bring-up.

Useful variations:

```bash
(cd new/user && ./debug.sh tuning --turn-suppressed --sequence 25,35,45)
(cd new/user && ./debug.sh tuning --no-plot --csv ../verification/phase-d-speed-tuning-headless.csv)
```

`--no-plot` is a headless troubleshooting path only. It intentionally bypasses plotting fallback and should not be used as the preserved fallback-proof transcript for checkpoint-4 verification.

Local-only host verification can use the simulator peer when new board execution is intentionally skipped:

```bash
(cd new/user && ./.host-verify-venv/bin/python tune_speed.py \
  --listen-host 127.0.0.1 \
  --listen-port 8899 \
  --sequence 20,40,60,77 \
  --disabled-mode-checks \
  --invalid-target-speed 90 \
  --csv ../verification/phase-d-speed-tuning-local-sim.csv) &
(cd new/user && python3 simulate_assistant_peer.py --host 127.0.0.1 --port 8899)
```

## Accepted Board Workflow

Before starting the host tool:

```bash
(cd new/user && ./debug.sh assistant local 8888 8890)
(cd new/user && ./debug.sh build)
(cd new/user && ./debug.sh remote restart normal)
```

The frozen board-side control/media wiring surface is:

1. `assistant_tcp.host`
2. `assistant_tcp.port`
3. `steering_media_enabled`
4. `steering_media_port`
5. `steering_media_publish_interval_ms`

Host-side tooling may configure or read this accepted surface through the project-owned setup path, but it must remain read-only with respect to steering gains and other PID writes.

If the run is local-only and the architecture is compatible:

```bash
(cd new/user && \
  LS2K_PARAMS_PATH=../verification/params/assistant-local-smoke.json \
  LS2K_PROFILE_PATH=../config/hardware_profile.json \
  ./debug.sh smoke local)
```

## Required Evidence

For an accepted Phase D tuning run, preserve:

1. host CSV capture
2. host stdout log showing accepted and rejected ACKs
3. board runtime log with `assistant.*`, `motion.*`, `control.snapshot`, and `control.steering_snapshot`
4. steering media `config_snapshot.json`, `frame_metadata.jsonl`, `frames/frame-*.raw`, `frame_control_alignment.jsonl`, and `alignment_summary.json`
5. any optional assistant waveform/image capture used as support evidence only

Required contract evidence:

1. one explicit rejected `set_target_speed` while tuning mode is off
2. one explicit rejected `set_turn_suppressed` while tuning mode is off
3. at least one rejected invalid target speed above `Speed_base`
4. one `snapshot_cleared` event caused by `disable_tuning_mode`
5. `target_speed_override_value=null` when no override is active
6. `effective_speed_target=0` in non-driving phases
7. one standalone `input_rejected` state frame preserved in the host transcript and CSV
8. one preserved host transcript showing plotting fallback behavior, either successful install-attempt fallback or explicit CSV-only fallback after install failure; this transcript may be preserved as a separate local-sim host artifact instead of the main steering-media board bundle

For local-only verifier reruns, the preserved evidence bundle may be composed from:

1. `phase-d-speed-tuning-local-sim.csv`
2. `phase-d-speed-tuning-local-sim-host.log`
3. `phase-d-speed-tuning-local-sim-peer.log`
4. `phase-d-speed-tuning-local-sim-fallback.csv`
5. `phase-d-speed-tuning-local-sim-fallback-host.log`
6. `phase-d-speed-tuning-local-sim-fallback-peer.log`
7. the reviewed source for `tune_speed.py` and `simulate_assistant_peer.py`

## What The Evidence Proves

Accepted evidence proves:

1. remote commands are decoded above transport and correlated by `seq`
2. remote `start` / `stop` flow through `motion_intent`
3. tuning snapshot is volatile and clears independently of static params
4. raw turn and applied turn remain separately observable
5. host tooling can tune repeatedly without redefining the board protocol
6. host tooling records standalone state events including `input_rejected`
7. host plotting fallback behavior is preserved even when `matplotlib` is unavailable in the host runtime, whether that proof comes from the board bundle or a dedicated local-sim transcript
8. steering metadata and raw image evidence stay read-only and time-alignable with the control CSV

Accepted evidence does not prove:

1. that new PID gains are good enough for race use
2. that manual Seekfree UI rendering is correct
3. that the runtime can tune safely without the documented enable/start/stop/disable sequence
