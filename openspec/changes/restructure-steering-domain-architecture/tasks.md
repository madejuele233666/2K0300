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
  - `subject_required_any_of`
  - `findings_required`
  - `finding_object_required`
  - `finding_semantics`
  - `repair_routing_rules`
  - `valid_pass_requirements`
  - `partial_scope_rule`
- Verifier invocation:
  - use the built-in subagent API with `fork_context=false`
  - use invocation template `verify-reviewer-inline-v3`
- Routing target for blocking findings:
  - `openspec-repair-change`
- Supported continuation overrides:
  - `verify-only`
  - `dry-run`
  - `manual_pause`
- Artifact-completion gate ownership:
  - when this task list completes the schema's `applyRequires` set under `ai-enforced-workflow`, the active artifact-creation caller (`openspec-propose` or `openspec-continue-change`) runs docs-first review before implementation entry
  - `openspec-apply-change` does not own that docs-first artifact gate

## 1. Preflight and Current-State Inventory

- [x] 1.1 Record dirty-worktree boundaries with `rtk git status --short`; protect unrelated user changes and avoid reverting existing modified files unless explicitly requested.
- [x] 1.2 Confirm active architecture wording in `README.md`, `docs/WORKFLOW.md`, `new/code/port/README.md`, and `new/docs/visual-element-sparse-circle-v1.zh-CN.md`.
- [x] 1.3 Generate a current source inventory for `new/code/legacy`, `new/code/runtime`, `new/code/platform`, `new/code/port`, `new/user/CMakeLists.txt`, and `new/verification/tests`.
- [x] 1.4 Record the implementation review surfaces for source-first verification: moved source paths, changed build files, changed verification scripts, changed C++ tests, active docs, and direct dependency scans.
- [x] 1.5 Decide the shared motion-history contract destination, expected default `port/motion_history_types.hpp`, and update `design.md` if implementation discovers a better port-owned location.
- [x] 1.6 Run `rtk openspec status --change restructure-steering-domain-architecture --json` and record that the artifact set is implementation-ready only after docs-first verification passes.
- [x] 1.7 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the docs-first artifact bundle. Use the built-in subagent API with `fork_context=false` and invocation template `verify-reviewer-inline-v3`. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`, including `subject_required_any_of`, `findings_required`, `finding_object_required`, `finding_semantics`, and `repair_routing_rules`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `review/review-runs/restructure-steering-domain-architecture/docs-first/attempt-<n>/findings.json`, verifier evidence JSON at `review/review-runs/restructure-steering-domain-architecture/docs-first/attempt-<n>/verifier-evidence.json`, and caller/orchestrator-maintained `review/review-runs/restructure-steering-domain-architecture/agent-table.json`.

## 2. Vertical Slice: Control Domain Ownership

- [x] 2.1 Move `runtime/motion_types.hpp`, `runtime/motion_supervisor.*`, and `runtime/tuning_state.*` into `new/code/control/` with includes updated but no state-machine or tuning behavior changes.
- [x] 2.2 Move `legacy/steering_yaw_controller.*`, `legacy/wheel_pid.*`, `legacy/wheel_target_mixer.*`, and `legacy/actuator_command_builder.*` into `new/code/control/`.
- [x] 2.3 Update `runtime/control_loop.*`, `runtime/assistant_service.*`, `runtime/runtime_state.hpp`, tests, and `new/user/CMakeLists.txt` to use `control/` paths.
- [x] 2.4 Update namespace ownership for this slice or document any temporary retained namespace in the task evidence; final active APIs must not remain under `ls2k::legacy`.
- [x] 2.5 Run focused verification: `rtk new/verification/tests/run_wheel_target_mixer_test.sh`, `rtk new/verification/tests/run_reference_usability_lateral_error_test.sh`, and `rtk new/verification/tests/run_assistant_telemetry_selftest.sh`.
- [x] 2.6 Run residual checks for stale control paths: `rtk rg -n "legacy/(steering_yaw_controller|wheel_pid|wheel_target_mixer|actuator_command_builder)|runtime/(motion_types|motion_supervisor|tuning_state)" new/code new/user new/verification/tests`.

## 3. Vertical Slice: Safety Gate and Low-Voltage Ownership

- [x] 3.1 Split `runtime/control_decision.*` into `new/code/safety/control_gate.*` and `new/code/safety/control_apply_observation.*`.
- [x] 3.2 Preserve safety-gate veto priority exactly: `low_voltage`, `perception_stale`, `perception_invalid`, `reference_control_not_ready`, `imu_invalid`, `encoder_invalid`, `none`.
- [x] 3.3 Move or boundary-wrap `runtime/low_voltage_sampler.*` under safety ownership while preserving `RuntimeState` lock/atomic semantics, startup sample reuse, invalid-sample fail-safe, and transition diagnostics.
- [x] 3.4 Update `runtime/runtime_state.hpp`, `runtime/control_loop.*`, `new/user/main.cpp`, observability headers, CMake, and tests to include `safety/` paths.
- [x] 3.5 Run focused verification: `rtk new/verification/tests/run_power_adapter_threshold_test.sh`, `rtk new/verification/tests/run_startup_low_voltage_order_test.sh`, `rtk new/verification/tests/run_reference_usability_lateral_error_test.sh`, and `rtk new/verification/tests/run_bev_simple_residual_check.sh`.
- [x] 3.6 Run residual checks for stale safety paths: `rtk rg -n "runtime/(control_decision|low_voltage_sampler)|ControlVetoReason|ControlApplyOutcome" new/code new/user new/verification/tests`.

## 4. Vertical Slice: Transport and Observability Ownership

- [x] 4.1 Move `platform/assistant_protocol.*`, `platform/assistant_link.*`, `platform/steering_media_protocol.*`, `platform/steering_media_link.*`, and `platform/visual_element_evidence_json.hpp` into `new/code/transport/`.
- [x] 4.2 Keep `platform/true_ls2k0300/assistant_bridge.*` and `platform/true_ls2k0300/steering_media_bridge.*` under `platform/true_ls2k0300/`, and update transport `.cpp` files to call those bridge headers from the new layer.
- [x] 4.3 Move `runtime/control_debug_snapshot.hpp`, `runtime/control_debug_reporter.*`, and `runtime/assistant_telemetry_view.hpp` into `new/code/observability/`.
- [x] 4.4 Update `runtime/assistant_service.*`, `runtime/steering_media_service.*`, `runtime/control_loop.*`, `new/user/steering_media_selftest.cpp`, `new/verification/tests/assistant_telemetry_selftest.cpp`, CMake, and common steering test build scripts.
- [x] 4.5 Verify no JSON command, assistant telemetry field, steering-media config field, image envelope, or visual-element evidence JSON field is intentionally changed.
- [x] 4.6 Run focused verification: `rtk new/verification/tests/run_assistant_telemetry_selftest.sh`, `rtk new/verification/tests/run_steering_media_selftest.sh`, and `rtk new/verification/tests/run_host_capture_selftest.sh` if host-capture dependencies are available.
- [x] 4.7 Run residual checks for stale transport/observability paths: `rtk rg -n "platform/(assistant|steering_media|visual_element_evidence_json)|runtime/(control_debug|assistant_telemetry_view)" new/code new/user new/verification/tests`.

## 5. Vertical Slice: Reference Contract and Reference Domain

- [x] 5.1 Create the shared motion-history port contract, expected `new/code/port/motion_history_types.hpp`, and update `runtime/runtime_state.hpp` to store that type without changing ring-buffer capacity or ordering semantics.
- [x] 5.2 Update `runtime/steering_frame_perception_pipeline.*`, CircleV2 motion arc code, and `runtime/steering_reference_time_alignment.*` to consume the port-owned motion-history type.
- [x] 5.3 Move `runtime/steering_reference_time_alignment.*` into `new/code/reference/reference_time_alignment.*`.
- [x] 5.4 Extract hold/continuity functions from `legacy/steering_bev_simple_perception.*` into `new/code/reference/reference_continuity.*`.
- [x] 5.5 Move `legacy/steering_visual_reference_orchestration.*`, `legacy/steering_reference_usability.*`, `legacy/steering_reference_lateral_error.*`, `legacy/steering_reference_tracking_geometry.*`, and `legacy/steering_reference_control_readiness.*` into `new/code/reference/`.
- [x] 5.6 Update runtime pipeline, control loop, tests, CMake, and active docs for reference paths and namespace ownership.
- [x] 5.7 Run focused verification: `rtk new/verification/tests/run_reference_time_alignment_test.sh`, `rtk new/verification/tests/run_reference_usability_lateral_error_test.sh`, `rtk new/verification/tests/run_reference_tracking_geometry_test.sh`, and `rtk new/verification/tests/run_visual_reference_orchestration_test.sh`.
- [x] 5.8 Run dependency checks: `rtk rg -n '#include "runtime/' new/code/reference new/code/control new/code/safety new/code/observability new/code/vision new/code/transport`.

## 6. Vertical Slice: Vision and CircleV2 Ownership

- [x] 6.1 Move `legacy/steering_otsu_threshold.*` and `legacy/steering_bev_pixel_classifier.hpp` into `new/code/vision/image/`.
- [x] 6.2 Move `legacy/steering_bev_projector.*`, `legacy/steering_bev_simple_perception.*`, `legacy/steering_bev_element_raster.*`, `legacy/steering_reference_connectivity.*`, `legacy/steering_single_boundary_offset.*`, `legacy/steering_bev_interval_edges.hpp`, and `legacy/steering_bev_boundary_trace_clip.hpp` into `new/code/vision/bev/`.
- [x] 6.3 Move `legacy/steering_cross_exit_element_evidence.*`, `legacy/steering_circle_element_evidence.*`, `legacy/steering_visual_element_evidence.*`, and `legacy/steering_visual_element_pipeline.*` into `new/code/vision/elements/`.
- [x] 6.4 Move `runtime/steering_circle_v2_scene.*`, `runtime/steering_scene_frame_view.hpp`, `runtime/steering_circle_v2_reference_adapter.*`, and `runtime/detail/steering_circle_v2_*` into `new/code/vision/elements/circle_v2/` and `new/code/vision/elements/circle_v2/detail/`.
- [x] 6.5 Update includes, namespaces, `new/user/scene_overlay_probe.cpp`, CircleV2 tests, visual element tests, BEV tests, CMake, and active docs.
- [x] 6.6 Verify CircleV2 does not become runtime-owned again and that cross/circle Phase1 still consumes sparse BEV row facts, not full raster as a required hot-path input.
- [x] 6.7 Run focused verification: `rtk new/verification/tests/run_bev_simple_perception_test.sh`, `rtk new/verification/tests/run_visual_element_evidence_test.sh`, `rtk new/verification/tests/run_steering_circle_v2_scene_test.sh`, `rtk new/verification/tests/run_visual_reference_orchestration_test.sh`, and `rtk new/verification/tests/run_scene_overlay_probe_authority_baseline_test.sh`.
- [x] 6.8 Run residual checks: `rtk rg -n "legacy/steering_|runtime/(steering_circle_v2|steering_scene_frame_view)|new/code/legacy" new/code new/user new/verification/tests README.md docs/WORKFLOW.md new/docs`.

## 7. Vertical Slice: Runtime Subdirectory Organization

- [x] 7.1 Move `runtime/startup.*` and `runtime/shutdown.*` into `new/code/runtime/lifecycle/`.
- [x] 7.2 Move `runtime/camera_capture_worker.*` and `runtime/camera_frame_store.*` into `new/code/runtime/capture/`.
- [x] 7.3 Move and rename `runtime/steering_frame_perception_pipeline.*` to `new/code/runtime/pipelines/steering_frame_pipeline.*`.
- [x] 7.4 Move `runtime/control_loop.*` into `new/code/runtime/loops/`.
- [x] 7.5 Move `runtime/assistant_service.*` and `runtime/steering_media_service.*` into `new/code/runtime/services/`.
- [x] 7.6 Keep `runtime/runtime_state.hpp` as the shared runtime state owner and preserve shared mutex, atomics, frame-store lifetime, service tick semantics, and no-motion startup semantics.
- [x] 7.7 Update `new/user/main.cpp`, `new/user/CMakeLists.txt`, runtime includes, tests, active docs, and residual path checks.
- [x] 7.8 Run focused verification: `rtk new/verification/tests/run_camera_frame_store_test.sh`, `rtk new/verification/tests/run_startup_low_voltage_order_test.sh`, `rtk new/verification/tests/run_steering_media_selftest.sh`, and all pipeline-related tests from earlier slices.

## 8. Build Wiring, Residual Checks, and Active Documentation

- [x] 8.1 Update `new/user/CMakeLists.txt` main source list and verification helper targets for all new paths.
- [x] 8.2 Update direct shell build scripts under `new/verification/tests/run_*.sh` and `new/verification/tests/common_steering_test_build.sh`.
- [x] 8.3 Update active documentation: `README.md`, `docs/WORKFLOW.md` only if routing changes, `new/code/port/README.md`, `new/docs/visual-element-sparse-circle-v1.zh-CN.md`, and active user/build docs that name moved paths.
- [x] 8.4 Keep archive/superseded documentation historical; add routing notes only where current docs require them.
- [x] 8.5 Delete active `new/code/legacy/` only after all migrated source, include, CMake, and verification references are removed.
- [x] 8.6 Run broad residual checks for stale active paths: `rtk rg -n "new/code/legacy|#include \"legacy/|runtime/steering_circle_v2|runtime/control_decision|runtime/low_voltage_sampler|runtime/control_debug|runtime/assistant_telemetry_view|platform/assistant_|platform/steering_media_" new/code new/user new/verification/tests README.md docs/WORKFLOW.md new/docs`.
- [x] 8.7 Run `rtk openspec validate --all` and record output.

## 9. Local and Board-Aware Verification

- [x] 9.1 Run focused tests from slices 2 through 7 again after final path cleanup.
- [x] 9.2 Run `rtk new/verification/tests/run_bev_simple_residual_check.sh` after all active path rewrites.
- [x] 9.3 Run the supported board build command, expected `rtk new/user/debug.sh build`, and record whether this is local/cross build evidence only.
- [x] 9.4 If board access is available and user has not restricted it, run no-motion board startup smoke using the repo-supported flow and record startup, safety gate, assistant, and steering-media evidence separately.
- [x] 9.5 Do not run or claim powered drive behavior unless the user explicitly requests powered drive and the safety preconditions are rechecked.

## 10. Verification and Review

- [x] 10.1 Confirm all implementation evidence paths and command outputs are listed in the source-first review bundle.
- [x] 10.2 Run `rtk openspec validate restructure-steering-domain-architecture --type change` and `rtk openspec status --change restructure-steering-domain-architecture --json`.
- [x] 10.3 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for source-first implementation correctness. Use the built-in subagent API with `fork_context=false` and invocation template `verify-reviewer-inline-v3`. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`, including `subject_required_any_of`, `findings_required`, `finding_object_required`, `finding_semantics`, and `repair_routing_rules`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `review/review-runs/restructure-steering-domain-architecture/source-first/attempt-<n>/findings.json`, verifier evidence JSON at `review/review-runs/restructure-steering-domain-architecture/source-first/attempt-<n>/verifier-evidence.json`, and caller/orchestrator-maintained `review/review-runs/restructure-steering-domain-architecture/agent-table.json`.
- [x] 10.4 If the active verifier returns `block`, route repairs through `openspec-repair-change`, rerun the same verifier lifecycle, and do not mark completion until a valid active pass has `review_coverage.coverage_status=complete`, `review_coverage.exhaustive=true`, and `unreviewed_axes=[]`.
- [x] 10.5 After a valid active pass, run final status and prepare archive/sync steps only if implementation has been completed and accepted.

## 11. Cleanup and Decision Capture

- [x] 11.1 Capture any resolved open questions in `design.md` or a task evidence note, especially motion-history contract placement and low-voltage runtime boundary.
- [x] 11.2 Remove any temporary namespace aliases, forwarding headers, or compatibility shims unless the verifier accepts a documented temporary compromise with follow-up scope.
- [x] 11.3 Ensure active docs explain the new ownership tree and do not present archive/superseded paths as current authority.
- [x] 11.4 Confirm no prototype-only files, temporary scripts, or stale generated binaries were added to tracked source paths.

## Implementation Evidence Note

- `rtk new/user/debug.sh build` completed CMake configure, compile, link, and `Built target new`; the command returned non-zero only after the post-build upload attempted `ssh root@10.100.170.226` and timed out.
- Focused local tests passed for wheel target mixer, reference time alignment, reference usability/lateral error, assistant telemetry, BEV simple perception, visual element evidence, CircleV2 scene, visual reference orchestration, reference tracking geometry, steering media selftest, startup low-voltage order, camera frame store, power adapter threshold, BEV residual, and host capture.
- `rtk bash new/verification/tests/run_scene_overlay_probe_authority_baseline_test.sh` links after the new `reference_continuity.cpp` split but still fails the `circle-2-confirmed-innertrace` expectation (`circle_v2.frame_phase=inner_trace` absent; output stays `approach/approach`). Per user direction, this scene-overlay behavioral failure is accepted as non-blocking for this structure-only change and is not reported as passed.
- Residual active-path scan for `new/code/legacy`, stale runtime CircleV2/safety/observability paths, stale transport platform paths, and `steering_frame_perception_pipeline` returned no matches outside archive/superseded paths.
- Dependency scan over `new/code/reference`, `new/code/control`, `new/code/safety`, `new/code/observability`, `new/code/vision`, and `new/code/transport` found no `runtime/` includes or `ls2k::runtime` namespace references; `transport` only calls `platform::true_ls2k0300` bridge APIs.
- `rtk openspec validate restructure-steering-domain-architecture --type change` passed, and `rtk openspec validate --all` reported 16 passed, 0 failed.
