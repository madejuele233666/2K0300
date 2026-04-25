# Orchestrator Input Fixtures

These fixtures demonstrate the current-state-only orchestrator inputs used by
`verification_cycle_resolve.py`.

- `no-active-agent.input.json` -> `spawn_active(no_usable_active_agent)`
- `active-block.input.json` -> `enter_repair`
- `block-to-pass.input.json` -> `mark_non_active`
- `active-valid-pass.input.json` -> `terminate`
- `send-input-agent-not-found.input.json` -> `resume_active`
- `active-agent-missing.input.json` -> `spawn_active(active_agent_missing)`
- `active-agent-not-resumable.input.json` -> `spawn_active(active_agent_not_resumable)`
