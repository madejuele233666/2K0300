## ADDED Requirements

### Requirement: Topology Scene FSM MUST Consume TopologyEvidence
The formal scene FSM MUST consume `TopologyEvidence` and prior FSM state as its decision authority. It MUST own candidate, confirmation, progress, and release state, and MUST NOT read old observation booleans or compatibility geometry fields as formal scene authority.

#### Scenario: Scene confirmation is evidence-based
- **WHEN** cross, circle, zebra, ordinary, or lost evidence changes over multiple frames
- **THEN** the FSM MUST confirm, progress, hold, reacquire, or release according to topology evidence and configured hysteresis
- **AND** old fields such as width expansion, open score, or bottom transition density MUST NOT be the formal decision source

### Requirement: Bend MUST Remain Ordinary Corridor Curvature
Bend behavior MUST NOT be represented as a formal special scene or separate control branch. Bend evidence MAY be exposed as ordinary curvature, curvature consistency, bend-veto score, lookahead adjustment, speed/constraint adjustment, or cross/circle veto.

#### Scenario: Large ordinary bend does not enter a special scene
- **WHEN** the ordinary corridor has high but consistent curvature and no topology evidence for cross or circle
- **THEN** the FSM MUST remain ordinary or compatible `straight` on the formal active surface
- **AND** control behavior MAY change only through reference curvature, lookahead, constraints, or speed scaling

### Requirement: Cross FSM MUST Use Bilateral Opening And Forward Reacquire Evidence
Cross confirmation MUST require topology evidence consistent with bilateral opening sync, short-distance width expansion or interval split, forward ordinary reacquire, and arc veto. Cross behavior MUST progress through approach, hold, reacquire, and release phases.

#### Scenario: Cross hold and reacquire preserve reference ownership
- **WHEN** cross evidence is confirmed and the near corridor becomes ambiguous
- **THEN** the FSM MUST enter cross approach or hold phase
- **AND** reference policy MUST use hold-last or entrance-heading extension
- **AND** reacquire MUST blend to forward-exit or ordinary hypotheses before release

### Requirement: Circle FSM MUST Score And Latch Direction
Circle evidence MUST be scored separately for left and right directions from single-side opening, opposite boundary continuity, same-sign curvature, arc or inner-offset hypothesis, and exit-corridor appearance. Once confirmed, circle direction MUST be latched until release.

#### Scenario: Confirmed circle direction does not flip on one frame
- **WHEN** a left or right circle is confirmed and later one frame has stronger opposite-side evidence
- **THEN** the FSM MUST keep the latched circle direction
- **AND** it MUST release only through accepted exit or ordinary-recovered conditions rather than direction flipping

### Requirement: Circle Reference MUST Progress Through Entry, Interior, Exit, And Release
Circle reference behavior MUST be selected by reference policy from topology hypotheses and FSM phase. Entry MAY blend ordinary to arc or inner-offset. Interior MAY use stable-boundary offset or arc follow. Exit MUST blend to exit or ordinary before release.

#### Scenario: Circle interior still uses reference policy
- **WHEN** the FSM is in circle interior
- **THEN** reference policy MUST select an accepted boundary-offset or arc-follow reference path
- **AND** the control model MUST still compute steering through the standard curvature-command entry

### Requirement: Zebra Behavior MUST Flow Through FSM, Reference, And Constraints
Zebra evidence MAY use transverse transition information only after it is projected or aggregated as BEV-local topology evidence. Zebra behavior MUST NOT bypass reference policy, constraints, or control-error model.

#### Scenario: Zebra hold does not directly command motors
- **WHEN** zebra evidence is confirmed
- **THEN** the FSM MAY enter zebra hold
- **AND** behavior MUST be expressed through reference hold, speed/constraint changes, or steering suppression
- **AND** no zebra-specific direct wheel/PWM branch MAY be introduced

### Requirement: Lost Behavior MUST Degrade Through Prediction, Constraints, Or Fail-Safe
When ordinary, cross, circle, and zebra evidence are all low-confidence, the topology pipeline MUST expose lost evidence. Runtime behavior MAY use short lost prediction up to a configured hold limit, then MUST degrade through constraints, steering suppression, or fail-safe gating.

#### Scenario: Lost confidence exceeds hold cycles
- **WHEN** topology evidence remains lost after the accepted hold limit
- **THEN** reference policy MUST stop extending a high-confidence path
- **AND** control constraints MUST suppress or fail-safe gate steering according to accepted safety policy

### Requirement: ReferencePolicy MUST Consume RoadHypotheses And FSM State
Reference policy MUST generate the active `BEVReferencePath` from `RoadHypotheses`, topology FSM state, prior reference memory, and runtime parameters. It MUST NOT read the raw image or old compatibility fields.

#### Scenario: Reference path changes only from topology inputs
- **WHEN** the FSM phase changes from ordinary to cross hold, circle interior, zebra hold, lost prediction, or release
- **THEN** reference policy MUST choose or blend an accepted topology-derived path
- **AND** it MUST NOT need raw image pixels or legacy image-row fields to generate that path

### Requirement: Control MUST Consume Curvature, Constraints, And Reference Path Only
The controller-facing steering behavior MUST be explained by `ControlErrorModelOutput.curvature_command`, reference path, vehicle context, and control constraints. Scene type changes MUST NOT directly alter wheel targets, PWM, or PID branch selection.

#### Scenario: Scene name alone does not change wheel or PWM output
- **WHEN** two control cycles have the same reference path, vehicle context, and constraints but different scene labels
- **THEN** the control model and downstream wheel target behavior MUST remain unchanged except for fields derived from those same inputs
- **AND** any scene effect on control MUST be mediated by reference path or constraints
