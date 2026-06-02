## Why

The steering media sidecar already carries raw camera frames and steering snapshots, but the accepted host workflow only records evidence to disk. Operators need a live browser view of the same media facts without coupling board runtime, evidence recording, and UI rendering together.

## What Changes

- Add a host-side live web viewer for steering media frames using a local HTTP/WebSocket server.
- Keep the board media protocol unchanged: the board still pushes `config_snapshot` and `image_frame` envelopes to the configured host media port.
- Refactor host media ingest so TCP envelope decoding, evidence recording, and live web fan-out are separate components.
- Preserve the existing `host_capture.py` evidence bundle behavior while allowing optional live viewing from the same incoming media stream.
- Add focused local tests for media decoding, evidence recording, and live broadcast behavior without requiring board hardware.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `steering-tuning-media-observability`: add host-side live web viewing of the read-only steering media stream while preserving the existing evidence bundle and board protocol contract.

## Risk Tier

- `STANDARD`: the change adds a host-facing HTTP/WebSocket surface and asynchronous fan-out behavior, but it does not change board runtime control decisions, motor safety gates, camera capture ownership, or the accepted steering media envelope contract.

## Impact

- Affected host tooling: `new/user/host_capture.py` and any new host-side web assets or helper modules under `new/user/`.
- Affected tests: focused host capture/live-viewer selftests under `new/verification/tests/`.
- Affected specs: `openspec/specs/steering-tuning-media-observability/spec.md` via this change's delta spec.
- Runtime impact: none intended on `port/`, `platform/`, `legacy/`, `runtime/`, or `config/` board code.
- Dependencies: Python standard library only for the server path unless an existing dependency is already required by the tool.
