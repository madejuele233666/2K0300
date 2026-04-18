# Transition Resolver Test-Harness Archive

This archive contains the development-period regression harness that was used to
stress the deterministic transition resolver before production rollout.

Archived contents:

- `fixtures/`
- `bin/run_transition_resolver_fixture_suite.py`
- `bin/run_transition_resolver_sequence_suite.py`
- `bin/run_working_session_normalizer_fixture_suite.py`

Operational surface remains outside this archive:

- `../../contracts/transition-resolver-input-v1.json`
- `../../contracts/transition-resolver-routing-v1.json`
- `../../contracts/transition-resolver-decision-v1.json`
- `../../bin/transition_resolver_validate.py`
- `../../bin/transition_resolver_resolve.py`
- `../../CALLER-INTEGRATION.md`

Use this archive only when you need to rerun or study the construction-period
resolver stress cases.
