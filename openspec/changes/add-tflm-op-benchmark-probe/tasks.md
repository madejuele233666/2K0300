## 0. Verification Contract

- Shared sequence:
  - `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`
- Shared JSON verification contract:
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`
- Shared field groups:
  - `invocation_common_required`
  - `output_paths_required`
  - `verifier_evidence_required`
  - `valid_pass_requirements`
  - `partial_scope_rule`
- Routing target for blocking findings:
  - `openspec-repair-change`

## 1. OpenSpec Artifacts

- [x] 1.1 Create `proposal.md`, `design.md`, `specs/tflm-op-benchmark/spec.md`, and `tasks.md` for `add-tflm-op-benchmark-probe`.
- [x] 1.2 Validate the change with `rtk openspec validate add-tflm-op-benchmark-probe --strict`.

## 2. Benchmark-only Build Isolation

- [x] 2.1 Add a tracked benchmark TFLM overlay CMake file so selected `unused` benchmark kernels are only compiled for benchmark builds.
- [x] 2.2 Add a benchmark-only `tflm_op_benchmark` target to `new/user/CMakeLists.txt`, gated by `LS2K_TFLM_OP_BENCH`, with no production runtime call sites.
- [x] 2.3 Verify ordinary `SKIP_UPLOAD=1 rtk new/user/debug.sh build` remains unaffected.

## 3. Board Benchmark Harness

- [x] 3.1 Add `board_tflm_op_benchmark.cpp` under `new/verification/tflm_op_benchmark/` with broad resolver registration, CSV output, embedded model invocation, timing, and no-motion behavior.
- [x] 3.2 Add generated-header contract files under the same verification directory, with defaults that are safe when no generated model exists yet.
- [x] 3.3 Verify the benchmark target can be cross-built from an isolated verification build directory.

## 4. Host Automation

- [x] 4.1 Add `generate_benchmark_assets.py` to convert available `.tflite` files into removable C arrays and a manifest.
- [x] 4.2 Add `run_board_tflm_op_benchmark.py` with preflight, build, upload, run, collect, parse, summarize, retry, timeout, and checkpoint/resume behavior.
- [x] 4.3 Verify host-only paths with `--build-only` and `--summarize-only` where board access is not required.

## 5. Result Contract

- [x] 5.1 Write results under `new/verification/tflm_op_benchmark/results/<timestamp>/`.
- [x] 5.2 Emit `board_ops_availability.csv`, `board_ops_benchmark.csv`, `board_model_benchmark.csv`, `summary.json`, `green_ops.txt`, `yellow_ops.txt`, and `red_ops.txt`.
- [x] 5.3 Classify resolver success, model success, timeout, allocation failure, invocation failure, and parse failure deterministically.

## 6. Review Gate

- [ ] 6.1 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for changed OpenSpec artifacts, CMake files, benchmark source, host scripts, and generated-header contract. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `openspec/changes/add-tflm-op-benchmark-probe/verification/source-first/attempt-<n>/findings.json`, verifier evidence JSON at `openspec/changes/add-tflm-op-benchmark-probe/verification/source-first/attempt-<n>/verifier-evidence.json`, and caller/orchestrator-maintained `openspec/changes/add-tflm-op-benchmark-probe/verification/source-first/agent-table.json`.
