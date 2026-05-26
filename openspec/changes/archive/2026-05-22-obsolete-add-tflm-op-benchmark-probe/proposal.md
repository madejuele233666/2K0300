## Why

Model 3.0 needs aggressive board-side inference optimization, but the usable and fast TFLite Micro operator set on the LS2K0300 board is unknown. We need a removable benchmark harness that can compile broad TFLM operators, run no-motion board tests automatically, and turn measured results into an operator whitelist for model search.

## What Changes

- Add a benchmark-only TFLM build path that can include selected `tensorflow/lite/micro/kernels/unused` sources without changing the production runtime.
- Add a no-motion board benchmark binary that registers the candidate operators, runs embedded `.tflite` models, and prints parseable CSV rows.
- Add a host-side automation script under `new/verification/tflm_op_benchmark/` that generates benchmark assets, builds, uploads, runs, collects logs, resumes from checkpoints, and summarizes green/yellow/red operators.
- Keep all benchmark code removable through one verification directory plus a small CMake conditional block.

## Capabilities

### New Capabilities

- `tflm-op-benchmark`: Covers benchmark-only TFLM operator compilation, board execution, host automation, result classification, and removable isolation from the production runtime.

### Modified Capabilities

- None.

## Risk Tier

- `STANDARD`: The production runtime remains unchanged by default, but this change touches cross-build configuration, vendor TFLM static-library build behavior, board upload/run automation, and benchmark binaries that execute on hardware.

## Impact

- `new/user/CMakeLists.txt`: adds a benchmark-only target guarded by `LS2K_TFLM_OP_BENCH`.
- `new/verification/tflm_op_benchmark/CMakeLists.txt`: tracked overlay build for the ignored vendor TFLM tree, with selected unused-kernel inclusion and configurable archive output.
- `new/verification/tflm_op_benchmark/`: contains the board benchmark source, generated-model header path, host automation, checkpoint/result files, and documentation.
- `openspec/changes/add-tflm-op-benchmark-probe/`: documents the change and verification contract.
