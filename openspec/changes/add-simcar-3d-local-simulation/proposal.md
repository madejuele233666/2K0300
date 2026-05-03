## Why

Local board-only validation finds camera/perception and device-integration failures too late, after deployment or track-side runs. This change introduces a host-local 3D simulation path that can run the existing target runtime against simulated LS2K0300 devices, with image-algorithm failure discovery as the first priority.

## What Changes

- Add a host-only `simcar/` simulation stack that launches the existing runtime as a read-only target and feeds it simulated LS2K0300 camera, IMU, encoder, motor, ADC, timer, and persistence behavior.
- Add a C++ replacement for the target-facing `ls2k::platform::true_ls2k0300` bridge surface for simulation builds, while leaving the production board bridge path unchanged.
- Add a Python MuJoCo live 3D simulator server reached through generated gRPC/Protobuf over a local Unix domain socket.
- Add `simcar run` orchestration for run directories, effective parameter generation, target-process launch, stdout/stderr capture, replay metadata, scenario selection, and artifact collection.
- Add first-stage scenario coverage for `bend`, `cross`, `circle`, `straight`, `lost-line`, `overexposure`, and `shadow`.
- Add documentation for the embedded target shadow contract and first-stage runtime-mode limits.

## Capabilities

### New Capabilities

- `local-simcar-3d-simulation`: host-local 3D simulation, bridge replacement, run orchestration, scenario/replay artifacts, and simulated LS2K0300 device behavior for the read-only target runtime.

### Modified Capabilities

- None. This is an additive host-side simulation capability and does not change existing production runtime requirements.

## Risk Tier

- `STRICT`: this change adds a cross-language C++/Python simulator path, generated gRPC/Protobuf IPC, replacement platform bridge symbols, 3D rendering dependencies, local run orchestration, and a new validation workflow around camera/platform/runtime behavior. It affects platform-adjacent integration and runtime validation even though the production board path remains unchanged.

## Impact

- Affected layers: host simulation tooling, C++ platform bridge replacement, runtime launch/config orchestration, local verification artifacts, and simulator documentation.
- Read-only target inputs: `new/user/main.cpp`, `new/code/*`, and the target configuration shape are consumed through an embedded shadow contract; implementation must not mutate tracked files under `new/`.
- Production board path: unchanged unless a later explicit change modifies it.
- New dependencies: gRPC/Protobuf generation/runtime, Python simulator dependencies, MuJoCo for live 3D rendering, and optional future BlenderProc support for offline high-fidelity generation.
- Participating skills: `openspec-propose` for this proposal and later `openspec-apply-change` / verification workflow skills for implementation.
