## Why

The current assistant TCP sidecar in `new/` is intentionally read-only, so engineers can observe wheel-level telemetry but cannot drive repeatable live speed-tuning runs from the host. Phase D tuning needs a project-owned way to start/stop motion, override target speed at runtime, and temporarily suppress turn output for straight-line PID tuning without mutating static params or contaminating the normal runtime path.

## What Changes

- Add a project-owned bidirectional assistant command surface on top of the existing TCP sidecar so the host can send `start`, `stop`, tuning-mode, turn-suppression, and target-speed override commands.
- Add a runtime-owned tuning state boundary that keeps live speed overrides, request-correlation bookkeeping, and tuning-only flags separate from `default_params.json` and normal startup-loaded parameters.
- Add a tuning-specific control profile that can temporarily suppress applied turn output while preserving the existing turn computation and normal runtime behavior outside tuning mode.
- Extend project-owned telemetry so host tools can receive structured state/ACK updates plus wheel-level target/measured/PWM data suitable for minimal live plotting and CSV capture.
- Add a lightweight host tuning workflow using Python and `matplotlib`, including installation fallback when the package is missing, to visualize left/right target-vs-measured speed curves during repeated PID tuning runs.

## Capabilities

### New Capabilities
- `runtime-speed-tuning-surface`: Define the bidirectional runtime tuning contract, including command semantics, runtime override isolation, tuning-mode behavior, and host-side live tuning workflow.

### Modified Capabilities
- `assistant-telemetry-sidecar`: Change the first-release assistant boundary from strictly read-only to a controlled bidirectional surface with project-owned command parsing, ACK/state feedback, and isolation rules.
- `dual-wheel-motion-control`: Extend the wheel-control contract so project-owned telemetry and tuning mode expose applied/raw turn data and runtime speed-override observability needed for PID tuning.
- `phase-b-motion-lifecycle`: Extend the lifecycle contract so project-owned remote operator intent may request `start` and `stop` without bypassing the accepted lifecycle boundaries.

## Risk Tier

- `STRICT`: This change introduces remote runtime control over motion start/stop and target-speed override, adds a tuning-only turn-suppression path inside the control chain, and changes the assistant-side contract from read-only telemetry to bidirectional command handling. The implementation must preserve fail-safe behavior, lifecycle boundaries, and normal-run isolation while adding a new live tuning workflow.

## Impact

- Affected code: `new/code/platform/assistant_link*`, `new/code/platform/true_ls2k0300/assistant_bridge*`, new runtime command/tuning-state modules, `new/code/runtime/assistant_service*`, `new/code/runtime/control_loop*`, `new/code/runtime/motion_supervisor*`, `new/code/runtime/runtime_state*`, and selected telemetry/debug snapshot surfaces.
- Affected runtime contract: assistant TCP sidecar, motion-intent ingress, runtime tuning state, structured telemetry, and tuning-only turn suppression.
- Affected tools/workflows: `new/user/debug.sh`, new host tuning script(s), verification runbooks, and Python host dependencies (`matplotlib` installed when absent).
- Participating skills: `openspec-propose` for artifact creation and `openspec-artifact-verify` for docs-first gate validation.

## Result References

- Closeout entrypoint for the late-stage wheel PID results:
  - [new/verification/auto-wheel-pid/20260423-closeout/README.md](../../../../new/verification/auto-wheel-pid/20260423-closeout/README.md)
- Promoted initial formal parameter summary:
  - [new/verification/auto-wheel-pid/20260423-formal-params-summary.md](../../../../new/verification/auto-wheel-pid/20260423-formal-params-summary.md)
