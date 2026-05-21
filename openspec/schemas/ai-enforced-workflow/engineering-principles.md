# Engineering Principles

Shared engineering discipline for `ai-enforced-workflow`.

This file is guidance, not a second verifier state machine. Schema and
verification-cycle contracts remain the authority for mandatory gates,
artifact fields, verifier evidence, and agent lifecycle.

## Principles

1. Alignment before artifacts.
   - If intent, scope, or terminology is ambiguous, resolve the highest-risk
     question before generating artifacts.
   - Ask one decision-bearing question at a time and include a recommended
     answer.
   - If the answer can be found by reading code, specs, logs, or evidence,
     inspect those sources before asking the user.

2. Domain language first.
   - Use existing project terms from `CONTEXT.md`, specs, design docs, and ADRs
     when naming capabilities, requirements, modules, tasks, tests, and
     findings.
   - If a term conflicts with existing project language, call out the conflict
     instead of silently inventing a synonym.
   - Missing `CONTEXT.md` or ADRs is a soft dependency: continue silently unless
     the user is explicitly asking to create or repair domain documentation.

3. Feedback loop first.
   - Before implementation repair, establish the fastest credible pass/fail
     signal for the issue: failing test, CLI command, local harness, captured
     trace replay, board/local evidence loop, or equivalent.
   - Rank hypotheses before changing code when the cause is not obvious.
   - Keep temporary instrumentation targeted and remove it before closure.

4. Vertical tracer slices.
   - Prefer small, independently verifiable slices that exercise an observable
     path through all relevant layers.
   - Avoid horizontal plans that batch all schema edits, then all code edits,
     then all tests.
   - Tests should verify behavior through public interfaces where practical,
     not private implementation shape.

5. Prototype answers a question.
   - Use a prototype only when it resolves a concrete design question faster
     than prose.
   - Mark prototype code as throwaway, keep it runnable with one command, and
     delete or absorb it once the decision is made.
   - Capture the durable answer in `design.md`, an ADR, or a task before moving
     on.

6. Hard dependencies stay small.
   - Hard dependencies are only the rules that make the workflow wrong if
     missing: `verify-sequence/default`, authoritative findings/evidence,
     valid-pass requirements, subject binding, and current-state
     `agent-table.json` (current-state `agent-table.json`).
   - Soft dependencies such as glossary, ADRs, architecture heuristics, and
     prototype notes improve output quality but must not become blocking gates
     unless a specific change makes them load-bearing.
