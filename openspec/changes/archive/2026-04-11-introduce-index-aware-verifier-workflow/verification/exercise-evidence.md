# Exercise Evidence

The files under `verification/exercises/` are exercise fixtures for the new
index-aware workflow contract. They are not authoritative checkpoint outputs and
do not replace the required verifier-subagent or Gemini evidence for checkpoint
tasks.

## 6.1 Reuse Without Unnecessary Deep Scan

- Preflight:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/reuse-preflight.json`
- Findings:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/reuse-findings.json`
- Verifier evidence:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/reuse-verifier-evidence.json`

This fixture shows `index_mode=reused`, `manifest_present=true`,
frozen `required_paths` / `required_axes`, `deep_scanned_paths=[]`,
`coverage_status=complete`, and
`saturation_status=exhaustive`.

## 6.2 Refresh Through Shared-Context Maintenance

- Preflight:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/refresh-preflight.json`
- Maintainer summary:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/refresh-maintainer-summary.json`
- Findings:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/refresh-findings.json`
- Verifier evidence:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/refresh-verifier-evidence.json`

This fixture shows preflight requesting refresh for governed agent coverage and
records that the maintainer updated index artifacts without emitting a final
review verdict before a fresh verifier consumed the refreshed manifest via
normalized `index_context`.

## 6.3 Triggered Deep Scan

- Preflight:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/deep-scan-preflight.json`
- Findings:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/deep-scan-findings.json`
- Verifier evidence:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/deep-scan-verifier-evidence.json`

This fixture shows a digest conflict escalating `schema.yaml` to source review
while keeping a per-run findings file aligned with the matching
`deep_scanned_paths` and `deep_scan_reasons`.

## 6.4 Role Boundary

- Boundary evidence:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/role-boundary.json`

This fixture records the intended contract split:
- `index-maintainer` may share main-process context and write index artifacts
- `verify-reviewer` remains fresh, read-only, and consumes only normalized
  `index_context`

## 6.5 Exhaustive Multi-Finding Review

- Preflight:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/saturation-preflight.json`
- Findings:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/saturation-findings.json`
- Verifier evidence:
  `openspec/changes/introduce-index-aware-verifier-workflow/verification/exercises/saturation-verifier-evidence.json`

This fixture shows one frozen governed review surface producing multiple
independent findings across more than one path and review axis while still
recording `coverage_status=complete` and `saturation_status=exhaustive`.
