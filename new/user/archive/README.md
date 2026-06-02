# Archived Host-Side Utilities

This directory keeps host-side workflows that are no longer the default path.

- `tune_steering.py`: legacy passive steering capture that also tails board SSH logs. Use through `./debug.sh steering legacy ...` when board-side `control.steering_snapshot` tail evidence is required.
- `tcp_reverse_relay.py`: old Windows-to-WSL callback relay. Use only as a forensic fallback when `./debug.sh steering host-capture ...` cannot bind or receive the board callback directly.

The canonical host callback path is `./debug.sh steering host-capture ...`, and `./debug.sh steering drive ...` uses that backend by default.
