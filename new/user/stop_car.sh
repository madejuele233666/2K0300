#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<'EOF'
Usage:
  ./stop_car.sh [now|controlled]

Modes:
  now         Default. Immediately stop the runtime process and disable actuators.
  controlled Send a controlled stop request first; if it does not exit in time, fall back to now.

Env:
  LS2K_CONTROLLED_STOP_TIMEOUT_S=5
EOF
}

mode="${1:-now}"
case "${mode}" in
    now|immediate|hard|force|controlled|graceful)
        shift || true
        ;;
    -h|--help|help)
        usage
        exit 0
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac

if [[ "$#" -ne 0 ]]; then
    echo "[ERROR] unexpected arguments: $*" >&2
    usage >&2
    exit 1
fi

exec "${SCRIPT_DIR}/debug.sh" remote stop "${mode}"
