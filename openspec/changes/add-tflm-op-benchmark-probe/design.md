## Context

The active `new/` runtime build does not link a TFLM benchmark path today. The vendor TFLM library has many candidate kernels available under `tensorflow/lite/micro/kernels/unused`, and `MicroMutableOpResolver` already exposes `Add*` methods for many of them. If those files are not compiled into the benchmark static library, broad resolver registration can fail at link time.

## Goals / Non-Goals

**Goals:**

- Build a broad TFLM benchmark binary without entering the main runtime loop.
- Automatically drive build/upload/run/collect/summary while the host-board SSH path is available.
- Keep all benchmark-only source and generated files easy to remove.
- Produce operator and model timing results suitable for Model 3.0 search decisions.

**Non-Goals:**

- Do not change production model inference, steering, motor, assistant telemetry, or normal runtime startup.
- Do not require TensorFlow on the current host for the baseline benchmark path.
- Do not decide the final Model 3.0 architecture in this change.

## Decisions

### Benchmark-only TFLM library

- Problem: candidate operator registrations reference kernels that live in `unused`.
- Alternatives considered: compile unused kernels into the production library, manually copy selected files, patch the git-ignored vendor CMake file, or build a benchmark-only static library through a tracked overlay.
- Decision: add a tracked overlay CMake project under `new/verification/tflm_op_benchmark/CMakeLists.txt`. It builds a benchmark-only `libtflm.a` from the ignored vendor TFLM sources, includes the selected `unused` kernels needed by the requested operator set, and places the archive under the verification build tree instead of overwriting the vendor `lib/` output.
- Verification hook: production build with the option off must not include the benchmark target or unused-kernel archive; benchmark build with the option on must link the broad resolver.

### Isolated board benchmark binary

- Problem: board operator tests must not affect the main control thread.
- Alternatives considered: add a command-line mode to `new/user/main.cpp`, or create a separate executable.
- Decision: create a separate `tflm_op_benchmark` target under `new/verification/tflm_op_benchmark/board_tflm_op_benchmark.cpp`, gated by `LS2K_TFLM_OP_BENCH`.
- Verification hook: ordinary `debug.sh build` invokes CMake without the benchmark option and continues to build only the normal `new` executable.

### Host automation with checkpointed resume

- Problem: board testing can be interrupted by SSH drops or a crashed benchmark process.
- Alternatives considered: manual board commands or one-shot shell script.
- Decision: implement a Python runner that checkpoints completed stages in `progress.json`, retries SSH/SCP operations, uploads a dedicated benchmark binary, starts it with `nohup`, collects the log, parses CSV rows, and writes summary files.
- Verification hook: the script supports `--build-only`, `--skip-upload`, and `--summarize-only` paths so host-side behavior can be checked without board access.

### Model asset generation fallback

- Problem: the current host environment may not have TensorFlow installed, so generating fresh operator micro-models is not always possible.
- Alternatives considered: require TensorFlow immediately, or commit binary generated test models.
- Decision: always generate a removable C header from available `.tflite` files, defaulting to the current deployment candidate if present. If TensorFlow is installed later, the same generator path may be extended to emit op-specific micro-models without touching production code.
- Verification hook: missing TensorFlow is recorded as a generator note, not as a production build failure.

### Removable test boundary

- Problem: aggressive benchmarking tends to leave long-lived test hooks in production code.
- Alternatives considered: keep reusable helper code in shared runtime modules, or isolate everything.
- Decision: all benchmark source, generated headers, logs, checkpoint files, and summaries live under `new/verification/tflm_op_benchmark/`; CMake changes are a single benchmark conditional block plus the TFLM option.
- Verification hook: deleting the verification directory and the CMake conditional block removes all benchmark code paths.

## Independent Verification Plan (STANDARD/STRICT)

Document verification using shared sequence `verify-sequence/default` from:
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`
and shared verification-cycle contracts:

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`

### Review Checkpoints

- Shared sequence reference: `verify-sequence/default`
- Review goal: `implementation_correctness`
- Verifier agent path: `.codex/agents/verify-reviewer.toml`
- Invocation template id: `verify-reviewer-inline-v3`
- Authoritative verifier-subagent findings JSON path: `openspec/changes/add-tflm-op-benchmark-probe/verification/source-first/attempt-<n>/findings.json`
- Verifier execution evidence JSON path: `openspec/changes/add-tflm-op-benchmark-probe/verification/source-first/attempt-<n>/verifier-evidence.json`
- Agent table path: `openspec/changes/add-tflm-op-benchmark-probe/verification/source-first/agent-table.json`
- Continuation target on pass: implementation closure

Checkpoint-specific primary surfaces:

- docs-first review: `proposal.md`, `design.md`, `tasks.md`, and `specs/tflm-op-benchmark/spec.md`
- source-first review: changed CMake files, benchmark source, host runner, generated-header contract, and build/upload behavior

## Migration Plan

- Default production behavior does not change because benchmark code is behind `LS2K_TFLM_OP_BENCH=OFF`.
- To run board tests, invoke the host runner with `--all --auto-advance`; it builds under `new/verification/tflm_op_benchmark/build/` and writes results under `results/<timestamp>/`.
- To roll back, remove `new/verification/tflm_op_benchmark/` and remove the benchmark block from `new/user/CMakeLists.txt`.

## Open Questions

- None for the implementation. Future Model 3.0 training will consume the benchmark summary to decide the final operator whitelist.

## Risks / Trade-offs

- Building all `unused` kernels can increase benchmark build time and binary size; this is accepted because the path is benchmark-only.
- Resolver registration proves compile/link availability, while precise op microbench timing requires op-specific `.tflite` cases. The baseline implementation still benchmarks real embedded models and leaves the generator path isolated for later expansion.
- Board automation assumes SSH/SCP remain available; when communication drops, checkpointed resume prevents losing completed results.
