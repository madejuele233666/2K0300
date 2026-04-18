# Transition Resolver Module

This module defines the machine-readable transition surface that sits above the
shared working/challenger review loop.

Use it when a caller needs to decide the next orchestration step from
normalized review state rather than from prompt prose.

The resolver is responsible for:

- deciding allowed next transitions
- preserving same-session working reruns
- enforcing fresh challenger entry
- recording challenger reopen transitions
- requiring the final run-dir mechanical gate before closure

Design stance:

- the resolver should be a deterministic local module by default
- it should not depend on a no-context subagent for final transition authority
- AI may still help produce normalized reviewer outputs or normalized input
  state, but final transition authority should remain mechanical

The resolver is not responsible for:

- performing review
- deciding whether findings are semantically correct
- editing files outside caller-declared repair scope

Authoritative files:

- `contracts/working-session-normalization-v1.json`
- `contracts/transition-resolver-input-v1.json`
- `contracts/transition-resolver-routing-v1.json`
- `contracts/transition-resolver-decision-v1.json`
- `bin/normalize_working_session.py`
- `bin/transition_resolver_validate.py`
- `bin/transition_resolver_resolve.py`
- `CALLER-INTEGRATION.md`
- `../REFERENCE-INDEX.md`

Archived construction-period test harness:

- `archive/test-harness-2026-04-17/README.md`

The decision contract stays orchestration-only.

Reviewer semantic judgment must remain in findings plus verifier evidence.
