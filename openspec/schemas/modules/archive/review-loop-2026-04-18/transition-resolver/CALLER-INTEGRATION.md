# Transition Resolver Caller Integration

Use this file when a workflow caller needs to drive the shared review loop
rather than merely describe it.

This file is the integration surface for:

- OpenSpec schema entrypoints
- standalone module-root verification
- repair flows that must decide whether to rerun, challenge, reopen, or close

## Rule

The caller MUST normalize `transition-resolver-input-v1`, run
`bin/transition_resolver_resolve.py`, and treat the returned
`transition-resolver-decision-v1` as the authoritative transition decision.

The caller MUST NOT:

- invent a different transition from prompt prose
- treat main-process inspection as equivalent to prior reviewer evidence
- replace built-in reviewer subagent invocation with shell/exec when the
  built-in path is available
- widen reviewer context beyond the minimal verification bundle plus optional
  `index_context` and `output_paths`
- bind `source_review`, current pass outputs, failed challenger outputs, or
  `reopen-record.json` to the wrong role directory; path role coherence is part
  of the mechanical contract

## Required Intent Sequence

Drive the loop with these intents:

1. `working_entry`
   Use when a review loop starts or when caller-local state says a working
   session must be recovered.
2. `working_rerun`
   Use for an ordinary rerun after repair inside the current working baseline.
3. `post_working`
   Use immediately after a working pass writes authoritative findings and
   verifier evidence.
4. `challenger_entry`
   Use only after the caller validates the previous working recorded outputs
   using `source_review`.
5. `post_challenger`
   Use immediately after a challenger pass writes authoritative findings and
   verifier evidence.
6. `close`
   Use before claiming final closure.

## Decision Execution Table

- `spawn_initial_working`
  - write `spawn-decision.json`
  - spawn built-in `verify-reviewer`
  - use `fork_context=false`
  - pass minimal bundle only
- `send_input_same_working`
  - send the rerun instruction into the existing working reviewer session
  - this means an actual `send_input` to the recorded `active_working_agent_id`
  - do not spawn a fresh agent while merely reusing the rerun prompt text
- `resume_same_working`
  - resume the existing working reviewer session
  - then continue the ordinary rerun there
  - this means an actual `resume_agent` of the recorded
    `active_working_agent_id`, followed by rerun inside that same session
  - do not substitute a fresh agent just because the prior session looks
    psychologically \"finished\"
- `spawn_exception_working`
  - allowed only for explicit exception reasons such as
    `agent_not_found`, `session_completed_unresumable`, or
    `tooling_recovery`
  - write `spawn-decision.json`
  - spawn a fresh working reviewer
- `enter_repair`
  - route according to `reroute_destination`
  - keep the current working baseline unless a later resolver decision records
    a valid exception or challenger promotion
- `return_caller_flow`
  - continue the caller automatically in the same turn
  - for a converged working pass, this normally means immediately preparing and
    resolving `challenger_entry`
- `wait_for_user`
  - allowed only when the caller explicitly requested `verify-only`,
    `dry-run`, or `manual_pause`
- `spawn_fresh_challenger`
  - write `spawn-decision.json`
  - spawn a fresh reviewer session
  - do not reuse working reviewer state
- `record_challenger_reopen`
  - write `reopen-record.json`
  - promote the failed challenger session into the next working baseline
  - the next ordinary rerun uses that promoted working agent
  - callers must preserve that promoted working baseline in
    `session.active_working_agent_id`; they must not silently fall back to a
    fresh `working_entry`
- `allow_close`
  - close only after the declared mechanical gate passed
- `deny_close`
  - do not close
  - return to the caller as blocked or incomplete
- `blocked`
  - stop the transition
  - surface the denial reason

## Required Input Normalization

Every caller-normalized input must include:

- `subject`
- `session`
- `review_result`
- `predecessor`
- `reopen`
- `caller`
- `authorization`
- `invocation`
- `paths`
- `mechanical_gate`

Use the contracts directly:

- `contracts/transition-resolver-input-v1.json`
- `contracts/transition-resolver-decision-v1.json`
- `contracts/transition-resolver-routing-v1.json`
- `contracts/working-session-normalization-v1.json`

## Working Session Normalization

Ordinary rerun correctness depends on caller-side session normalization.

Use:

- `transition-resolver/bin/normalize_working_session.py`
- `transition-resolver/contracts/working-session-normalization-v1.json`

The caller MUST mechanically derive `session` from explicit reuse evidence:

- `send_input_ready=true` only when the existing working agent can receive
  `send_input` directly now
- `resume_probe_attempted=true` only when the caller explicitly checked the
  inactive working session for reuse
- `resume_probe_result=resumable|unresumable|agent_not_found|tooling_error`
  only from that explicit reuse check

The caller MUST NOT:

- infer `session_completed_unresumable` from `wait_agent(...)=completed`
- infer `session_completed_unresumable` from
  `verifier-evidence.final_state=completed`
- treat a missing `verifier_session_id` as proof that resume is impossible
- use `working_rerun` without `active_working_agent_id`

Resolver rule:

- for `working_rerun`, fresh working spawn is legal only after a recorded
  failed reuse path
- without that normalization evidence, the resolver must block rather than
  silently open a fresh working session
- if `reopen.promoted_working_agent_id` is present, the caller must bind
  `session.active_working_agent_id` to that same agent; dropping it is a
  contract violation, not permission to restart the loop

## Entry-Point Defaults

Use these caller defaults unless the outer workflow explicitly overrides them.

### `openspec-artifact-verify`

- `review_phase=docs_first`
- after the artifact bundle is complete, immediately enter `working_entry`
- after converged working, immediately resolve `challenger_entry`
- if findings remain, route to `openspec-repair-change`

### `openspec-verify-change`

- `review_phase=source_first`
- use the same working/challenger loop semantics as docs-first review
- if findings remain, route to `openspec-repair-change`

### `openspec-repair-change`

- treat authoritative findings plus verifier evidence as the repair authority
- ordinary reruns MUST resolve through `working_rerun`
- after a failed challenger promotion, the promoted challenger becomes the
  active working baseline for the next rerun

### `openspec-propose`, `openspec-continue-change`, `openspec-ff-change`

- once the apply-required artifact set is complete, call
  `openspec-artifact-verify`
- do not stop after artifact creation unless the caller explicitly requested
  `verify-only`, `dry-run`, or `manual_pause`

### `openspec-apply-change`

- do not reopen the docs-first artifact gate
- when implementation verification is required, call
  `openspec-verify-change`

## Authorization And Invocation Rules

If a workflow entrypoint declares `verify-sequence/default`, invoking that
entrypoint explicitly authorizes reviewer-session creation for that loop only.

That authorization is bounded:

- reviewer sessions only
- minimal bundle only
- `fork_context=false`
- built-in reviewer subagent path first

The resolver must see that bounded authorization in normalized input before it
may return spawn decisions.

## Prompt Reduction Guidance

Callers should keep their prompt text thin.

Do not restate the full working/challenger loop in every caller prompt.

Instead:

- state the caller-specific review surface
- state caller-specific continuation or repair policy
- reference this file and `verification-sequence.md`
- execute the resolver mechanically
