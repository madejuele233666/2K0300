## ADDED Requirements

### Requirement: BEV simple perception is the active visual authority

The active steering perception path SHALL consume virtual BEV sparse samples when producing row white intervals and reference white points. Dense BEV and class images SHALL be debug artifacts only.

#### Scenario: White interval path is produced

- **WHEN** consecutive sparse BEV rows beginning at index 0 contain continuous white intervals
- **THEN** the active perception result SHALL publish reference points sourced from interval centers

#### Scenario: Far intervals do not bridge a near gap

- **WHEN** row index 0 is missing or the leading row sequence contains a gap
- **THEN** far intervals SHALL NOT be connected back into the near control path

#### Scenario: No current visual evidence

- **WHEN** the current BEV class image does not provide enough continuous white intervals
- **THEN** the active perception result MAY publish explicit hold points while hold memory remains valid
- **AND** hold points SHALL be marked with hold source

### Requirement: Archived implementation is inactive

Archived historical files SHALL NOT be part of active runtime targets, active helper tests, or active host tools.
