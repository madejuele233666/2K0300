# Plotting-Enabled Local Host Rerun

This bundle is a restrained follow-up to the earlier local-sim fallback proof.

Purpose:
- cover the host workflow path that had previously been skipped when `matplotlib` import failed
- confirm that `tune_speed.py` now runs the plotting-enabled path under the project `.venv`

Execution notes:
- host command used the project venv Python with `MPLBACKEND=Agg`
- the run intentionally did not use `--no-plot`
- peer traffic was provided by `new/user/simulate_assistant_peer.py`

Preserved artifacts:
- `phase-d-speed-tuning-local-sim-host.log`
- `phase-d-speed-tuning-local-sim-peer.log`
- `phase-d-speed-tuning-local-sim.csv`

What this proves:
- the host workflow no longer depends on the earlier `matplotlib import failed` fallback
- the plotting-enabled local-sim path still preserves accepted ACK/state/telemetry evidence
