## ADDED Requirements

### Requirement: Build Integration Treats Assistant As A New-Owned Sidecar
`new/user/` build integration SHALL compile any assistant sidecar implementation from the `new/` workspace while sourcing vendor assistant libraries only from the accepted true baseline, and assistant support SHALL remain optional at runtime.

#### Scenario: Assistant build graph stays inside the owned workspace boundary
- **WHEN** reviewers inspect `new/user/CMakeLists.txt` and related build files after this change
- **THEN** they SHALL find assistant sidecar source files owned under `new/`
- **AND** any vendor assistant source references SHALL resolve only to the accepted `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library` tree
- **AND** the accepted sidecar release surface for this change SHALL be TCP-only waveform publication plus E07_04-style image publication
- **AND** the build SHALL NOT require moving business logic into vendor example directories

### Requirement: Phase B Verification Uses Wheel-Level Evidence
The `new/` workspace documentation and verification flow SHALL describe dual-wheel control with wheel-level targets, wheel-level feedback, and wheel-level outputs, and SHALL treat assistant evidence as optional support evidence rather than as the acceptance gate itself.

#### Scenario: Phase B docs and evidence distinguish wheel-level behavior from sidecar convenience
- **WHEN** Phase B task docs, progress notes, or verification bundles are updated for this change
- **THEN** they SHALL name left/right target, left/right feedback, and left/right PWM observability as accepted evidence for the refactored control path
- **AND** they SHALL name at least one project-owned non-assistant evidence surface, such as structured diagnostics or harness-visible snapshot export, for assistant-disabled verification
- **AND** they SHALL describe assistant waveforms and image publication as optional convenience evidence that supports diagnosis but does not replace the primary runtime or board-test verdict
