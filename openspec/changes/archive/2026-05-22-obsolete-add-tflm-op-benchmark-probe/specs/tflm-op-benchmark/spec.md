## ADDED Requirements

### Requirement: Benchmark-only TFLM operator build

The system SHALL provide a benchmark-only TFLM build mode that can compile candidate operators from selected `tensorflow/lite/micro/kernels/unused` sources without enabling those kernels in the production runtime build by default.

#### Scenario: Production build remains default

- **WHEN** the normal `new/user` CMake build runs without `LS2K_TFLM_OP_BENCH`
- **THEN** the production runtime target SHALL build without the benchmark executable, benchmark model arrays, or benchmark resolver code.

#### Scenario: Benchmark build includes unused kernels

- **WHEN** the tracked benchmark TFLM overlay build is configured
- **THEN** the selected `tensorflow/lite/micro/kernels/unused` sources required by the candidate operator set SHALL be included in the benchmark static library build.

### Requirement: No-motion board benchmark binary

The system SHALL provide a board-side benchmark executable that does not start the normal runtime, motors, steering loop, camera loop, assistant service, or control thread.

#### Scenario: Benchmark process starts

- **WHEN** the benchmark binary is launched on the board
- **THEN** it SHALL only perform resolver registration and TFLM model allocation/invocation tests.

#### Scenario: Benchmark output is parseable

- **WHEN** a resolver or model benchmark case completes
- **THEN** the benchmark SHALL print one CSV row with a stable category, case name, operator list, status, timing fields, arena field, and error field.

### Requirement: Host-side automatic board test progression

The system SHALL provide a host runner that can automatically build, upload, run, collect, parse, summarize, and resume board TFLM benchmark tests.

#### Scenario: Auto-advance run

- **WHEN** the runner is invoked with `--all --auto-advance`
- **THEN** it SHALL execute preflight, asset generation, TFLM benchmark library build, benchmark binary build, upload, remote run, log collection, CSV parsing, and summary generation without per-stage prompts.

#### Scenario: Communication interruption

- **WHEN** an SSH/SCP operation fails temporarily
- **THEN** the runner SHALL retry and preserve completed stage state in `progress.json` so a later run can resume.

### Requirement: Removable test isolation

All benchmark-only source, generated assets, checkpoints, logs, and results SHALL be isolated from production runtime code and easy to remove.

#### Scenario: Test directory removal

- **WHEN** `new/verification/tflm_op_benchmark/` and the benchmark CMake conditional block are removed
- **THEN** no production source file SHALL require benchmark headers, generated arrays, logs, or runner files.

#### Scenario: Generated files stay in verification tree

- **WHEN** the host runner generates headers, manifests, checkpoints, or results
- **THEN** those files SHALL be written under `new/verification/tflm_op_benchmark/` only.

### Requirement: Result classification

The system SHALL summarize benchmark rows into green, yellow, and red operator/model result files for Model 3.0 search decisions.

#### Scenario: Successful core operator

- **WHEN** a candidate operator registers successfully and model cases using it complete within threshold
- **THEN** the summary SHALL classify it as green or yellow based on measured timing and status.

#### Scenario: Failed operator or model case

- **WHEN** a candidate operator fails registration, a model fails allocation or invocation, or a case times out
- **THEN** the summary SHALL classify the relevant case as red and keep the remaining cases runnable.
