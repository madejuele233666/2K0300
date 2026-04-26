## Archive Note

Archived on 2026-04-26 by explicit user request.

This change is archived as a transitional legacy-steering bridge, not as the
long-term steering architecture target. Its implemented and documented scope
established the 320x240 steering input contract, no-servo naming boundary,
`turn_output -> wheel_target_mixer -> wheel PID -> left/right PWM` terminal,
runtime-owned legacy steering state, and host/debug tooling alignment needed
before the BEV-first successor work.

Delta specs were intentionally not synced into `openspec/specs/`. The delta
specs under this archived change remain historical context only.

`tasks.md` is intentionally preserved as-is. At archive time the task list was
13/18 complete, with the remaining source-first verification checkpoints and
fixture work left unchecked. OpenSpec artifact status reported proposal,
design, specs, and tasks as complete for the `ai-enforced-workflow` artifact
gate.

The active successor is `bev-steering-perception-refactor`, which supersedes
the old pixel-domain geometry/control ownership by making BEV the sole
steering geometry truth and demoting legacy fields such as
`highest_line`, `farthest_line`, and `steering_reference_col` to compatibility
projection only.
