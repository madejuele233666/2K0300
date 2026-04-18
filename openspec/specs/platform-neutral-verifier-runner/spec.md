# platform-neutral-verifier-runner Specification

## Purpose
Define the platform-neutral second-opinion runner contract used by `ai-enforced-workflow` (for Gemini dual-verification paths), including input/output shape, retry/recovery behavior, and auditable command evidence.
## Requirements
### Requirement: Platform-Neutral Runner Contract
`ai-enforced-workflow` SHALL define second-opinion runner execution in terms of a platform-neutral contract rather than a hard-coded Windows PowerShell command. The contract MUST identify the required inputs, required outputs, retry semantics, and raw-envelope recovery behavior independently from the underlying shell implementation.

The runner contract MUST name:
- logical runner identifier
- prompt input paths or prompt source
- raw report path
- report path (`report_path`)
- maximum attempts
- JSON-response requirement
- recovery invocation using an existing raw envelope

#### Scenario: Workflow artifacts describe contract instead of shell-specific policy
- **WHEN** proposal, design, tasks, or schema instructions describe a verification step
- **THEN** they refer to the second-opinion runner contract and its required parameters
- **AND** they do not require a single platform-specific command string as the workflow policy itself

#### Scenario: Checkpoint artifacts preserve auditability
- **WHEN** a design, task, or execution log records a concrete verification checkpoint
- **THEN** it names the logical runner contract
- **AND** the active environment-resolved command is recorded in execution evidence or examples rather than replacing the logical policy text

### Requirement: Platform-Specific Runner Resolution
The second-opinion runner contract MUST support at least Windows and Linux resolution paths that preserve equivalent behavior. Windows resolution MAY invoke a PowerShell helper, and Linux resolution MAY invoke a shell helper, but both implementations MUST honor the same prompt, raw report, normalized report, retry, and recovery contract.

#### Scenario: Shared orchestration owns command resolution
- **WHEN** a verify-related skill or checkpoint needs to execute the runner
- **THEN** the shared verification sequence resolves the active platform command from the logical runner contract
- **AND** task text does not need to inline the full platform command to remain valid

#### Scenario: Linux helper satisfies the standard contract
- **WHEN** verification runs in a Linux environment
- **THEN** the selected shell helper accepts the declared runner inputs
- **AND** it writes the raw verifier envelope and normalized report to the declared output paths

#### Scenario: Windows helper satisfies the standard contract
- **WHEN** verification runs in a Windows environment
- **THEN** the selected PowerShell helper accepts the declared runner inputs
- **AND** it preserves the same normalized report expectations and recovery semantics used by Linux

### Requirement: Recovery Semantics Are Stable Across Platforms
The second-opinion runner contract MUST require retry-once behavior for primary execution failures and raw-envelope normalization recovery when the raw report exists but the normalized report is missing. Blocking verification failures SHALL only occur after both the primary execution path and the recovery path fail under the active platform resolver.

#### Scenario: Raw-envelope recovery works the same across helper implementations
- **WHEN** the primary second-opinion execution writes the raw envelope but does not produce the normalized report
- **THEN** the workflow invokes the contract's recovery path using the raw report input
- **AND** the verification step blocks only if recovery also fails

### Requirement: Execution Evidence Captures Runner Command And Verifier Invocation Metadata
Runner execution evidence SHALL capture the resolved platform command for each
verification iteration and the verifier invocation metadata (`agent_id`,
`start_at`, `end_at`, `final_state`) so checkpoints remain reproducible across
environments.

#### Scenario: Linux checkpoint records command and invocation metadata
- **WHEN** verification executes on Linux
- **THEN** execution evidence records the resolved Linux command
- **AND** execution evidence records verifier invocation metadata for the same iteration

#### Scenario: Windows checkpoint records command and invocation metadata
- **WHEN** verification executes on Windows
- **THEN** execution evidence records the resolved Windows command
- **AND** execution evidence records verifier invocation metadata for the same iteration
