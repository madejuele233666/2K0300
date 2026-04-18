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

The fixtures are intended for:

- guard validation examples
- orchestrator reasoning examples
- normalized output envelope validation
- documentation references
