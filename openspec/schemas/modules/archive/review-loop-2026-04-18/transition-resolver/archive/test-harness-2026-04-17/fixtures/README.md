# Transition Resolver Fixtures Archive

This directory contains the archived executable example inputs that were used to
stress the shared transition resolver during hardening.

Goals:

- provide positive examples for allowed transitions
- provide negative examples for valid-but-blocked transitions
- provide malformed inputs that must fail before any decision is produced
- cover easy-to-misroute situations such as challenger reopen promotion,
  broad context leakage, ordinary reruns without a reusable working session,
  and path-role coherence failures that often appear in real orchestration bugs

Run the full suite with:

```bash
python3 /home/madejuele/projects/2K0300/openspec/schemas/modules/review-loop/transition-resolver/archive/test-harness-2026-04-17/bin/run_transition_resolver_fixture_suite.py
```

Run the cross-caller sequence suite with:

```bash
python3 /home/madejuele/projects/2K0300/openspec/schemas/modules/review-loop/transition-resolver/archive/test-harness-2026-04-17/bin/run_transition_resolver_sequence_suite.py
```

Run the working-session normalizer suite with:

```bash
python3 /home/madejuele/projects/2K0300/openspec/schemas/modules/review-loop/transition-resolver/archive/test-harness-2026-04-17/bin/run_working_session_normalizer_fixture_suite.py
```

Case manifest:

- `cases.json`
- `sequences.json`
- `normalizer-cases.json`

Input categories:

- `positive/`
- `negative/`
- `invalid/`
- `normalizer/`
- `sequences/inputs/`
