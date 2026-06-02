## ADDED Requirements

### Requirement: Unified SpecialSceneFSM MUST Own Special-Scene Progression
The steering runtime MUST use one runtime-owned `SpecialSceneFSM` to own special-scene candidate confirmation, stage progression, and release. The accepted formal scene surface MUST NOT publish `active_module=special_wide`; any wide-suspect phase MUST remain internal candidate/debug state only.

#### Scenario: Wide-suspect evidence does not become a formal scene owner
- **WHEN** the perception stack observes ambiguous wide-geometry evidence before confirming cross or circle entry
- **THEN** the accepted FSM MAY retain that evidence internally
- **AND** the formal scene/module output MUST remain ordinary, cross, zebra, or a circle stage rather than `special_wide`

### Requirement: Scene Evidence MUST Consume BEV Observation And Vehicle Context Only
Circle, cross, zebra, and ordinary-bend veto logic MUST consume typed BEV scene observations and vehicle context rather than legacy compatibility projection fields or mixed current/history lane metrics.

#### Scenario: Circle entry is classified from structured BEV evidence
- **WHEN** the runtime evaluates whether a frame supports circle entry
- **THEN** it MUST use BEV-side branch, width, opposite-edge, and whitelist evidence plus accepted vehicle context
- **AND** it MUST NOT depend on compatibility-only image-space fields as its primary scene proof

### Requirement: ReferencePolicyResolver MUST Be Separate From FSM And Controller
The accepted architecture MUST separate scene progression from reference-path selection. The FSM SHALL own stage, while `ReferencePolicyResolver` SHALL choose the active reference path or path blend for ordinary tracking, circle entry, interior, exit, cross hold, zebra hold, and fallback behavior.

#### Scenario: A scene stage changes without changing control ownership rules
- **WHEN** the runtime transitions from `circle_entry_repair` to `circle_entry_settle` or `circle_interior`
- **THEN** the FSM MUST expose the new stage
- **AND** the reference policy layer MUST independently determine the accepted inner/outer/blended path behavior for that stage

### Requirement: Circle Progression MUST Latch After Confirmed Entry
Once circle entry is confirmed, the accepted FSM MUST progress through entry/interior/exit stages using process evidence and release criteria rather than requiring the original entry trigger to remain continuously visible.

#### Scenario: Entry evidence fades after repair begins
- **WHEN** circle entry has been confirmed and repair or settle has already begun
- **THEN** temporary loss of the original entry cue MUST NOT immediately release the FSM back to ordinary tracking
- **AND** release MUST depend on the accepted exit-complete and geometry-recovered conditions

### Requirement: New Observation Modules MUST Enter Through Typed Scene Or Constraint Inputs
Future control-affecting observation modules MAY influence scene behavior or control constraints, but they MUST do so through typed observation-merger outputs rather than by directly mutating FSM state or reference selection code.

#### Scenario: A future semantic module biases scene decisions
- **WHEN** a future observation module needs to provide a scene whitelist, blacklist, or additional evidence
- **THEN** it MUST contribute through the accepted observation-merger contract
- **AND** implementers MUST NOT bypass that contract by editing FSM state directly from the provider
