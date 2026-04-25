# Verification-Cycle Fixtures

These fixtures demonstrate the active `verification-cycle` semantics.

Each run dir contains:

- `agent-table.json`
- one or more `attempt-*` directories
- each `attempt-*` directory contains `findings.json`
- each `attempt-*` directory contains `verifier-evidence.json`

Additional verifier-output fixtures may contain:

- a normalized combined JSON envelope with top-level `findings` plus nested
  `verifier_evidence` under `fixtures/verifier-output/`
- current-state-only orchestrator input examples under
  `fixtures/orchestrator-inputs/`

The fixtures are intended for:

- guard validation examples
- orchestrator reasoning examples
- normalized output envelope validation
- documentation references

Review-run fixtures keep `agent-table.json` current-state-only even when the
directory still preserves older `attempt-*` history.
