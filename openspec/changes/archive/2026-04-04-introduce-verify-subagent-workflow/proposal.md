## Why

`ai-enforced-workflow` currently duplicates verification behavior across schema rules, templates, and verify skills, while also hard-coding a Windows-specific Gemini runner contract that no longer matches this repo's Linux-first execution path. The workflow needs a single verification orchestration model that isolates implementation from review, supports project-scoped reusable verifier agents, and preserves automatic repair routing without relying on full parent conversation context.

## What Changes

- Replace the current "implementer Codex reviews its own work, then Gemini verifies" narrative with an explicit verification model in which the implementer writes, the isolated verifier subagent performs the primary local review, and Gemini serves as a configurable second opinion when required by risk tier or an explicit double-verification gate.
- Introduce a project-scoped verify subagent contract for `ai-enforced-workflow` checkpoints; manual invocation may remain available as a convenience, but it is not itself a required acceptance target for this change.
- Require the verify subagent to run in read-only mode with minimal verification inputs instead of inheriting the full parent conversation, reducing context pollution and reviewer bias.
- Define a shared verification orchestration contract so artifact verification and implementation verification reuse the same flow, findings schema, retry behavior, repair routing rules, and a single verification-bundle contract.
- Enforce the same stateless verify/fix loop for any project using `ai-enforced-workflow`: each verify pass must spawn a fresh verifier instance, return normalized findings to the main flow, and rerun verification after eligible auto-fixes without inheriting prior verifier memory.
- Keep verifier runtime profile configuration explicit and project-controlled: default profile comes from `.codex/agents/verify-reviewer.toml`, and optional per-checkpoint override may be documented when needed.
- Replace platform-specific verifier command assumptions in schema/templates with a platform-neutral verifier runner contract that can resolve to Windows PowerShell or Linux shell helpers without changing the workflow policy.
- Add explicit auto-repair loop rules so the main agent can consume verifier findings, automatically fix implementation-layer issues only when `redirect_layer=implementation` and `auto_fixable=true`, rerun verification, and stop on configured retry or routing limits.
- **BREAKING**: `ai-enforced-workflow` design and task artifacts will need to describe verifier-subagent-first review, a tier-gated Gemini second-opinion policy, and platform-neutral runner contracts instead of Codex self-review plus a hard-coded PowerShell command.

## Capabilities

### New Capabilities
- `verify-subagent-orchestration`: Defines the isolated read-only verifier subagent, invocation contract, findings contract, Gemini second-opinion policy, and auto-repair loop behavior for artifact and implementation verification.
- `platform-neutral-verifier-runner`: Defines a workflow-level verifier runner contract that remains stable across Windows and Linux helper implementations.

### Modified Capabilities
- `ai-enforced-workflow`: Replaces implementer self-review wording with explicit `verify-reviewer` subagent invocation, adopts a platform-neutral verifier runner contract, introduces a shared verification-sequence reference, and tightens repair-loop and audit requirements for verification checkpoints.

## Risk Tier

- `STRICT`: This change rewires core workflow gates, verification sequencing, repair routing, and schema/template contracts. It affects artifact generation, apply-time behavior, cross-platform execution assumptions, and the trust boundary between implementer and verifier agents.

## Impact

- Affected code and artifacts: `openspec/schemas/ai-enforced-workflow/schema.yaml`, schema templates, verify-related skills, repair-flow instructions, and new project-scoped agent definitions under `.codex/`.
- Affected workflow behavior: artifact verification, implementation verification, checkpoint tasks, shared verification-sequence references, stateless verifier reruns, auto-repair reruns, and platform-specific Gemini runner resolution.
- Dependencies and systems: Codex subagent support, read-only sandbox policy for the verifier agent, Gemini runner helpers (`.ps1` and `.sh`), and OpenSpec change artifacts that must name verifier inputs/outputs explicitly.
- Participating skills: `openspec-propose`, `openspec-architect`, `openspec-artifact-verify`, `openspec-verify-change`, `openspec-repair-change`, and later `openspec-apply-change`.
