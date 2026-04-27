#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<'EOF'
Usage:
  ./start_with_upload.sh [no-motion|drive]

Modes:
  no-motion  Default. Stop runtime, build/upload, then start normal runtime with LS2K_AUTO_START=0.
  drive      Powered start. Requires CONFIRM_POWERED_START=1 and starts with LS2K_AUTO_START=1.

Examples:
  ./start_with_upload.sh
  ./start_with_upload.sh no-motion
  CONFIRM_POWERED_START=1 ./start_with_upload.sh drive
EOF
}

mode="${1:-no-motion}"
case "${mode}" in
    no-motion|--no-motion|safe|--safe)
        if [[ "$#" -gt 0 ]]; then
            shift
        fi
        auto_start="0"
        auto_start_delay_ms="${LS2K_AUTO_START_DELAY_MS:-0}"
        ;;
    drive|--drive|auto-start|--auto-start)
        shift
        if [[ "${CONFIRM_POWERED_START:-0}" != "1" ]]; then
            echo "[ERROR] drive mode requires CONFIRM_POWERED_START=1" >&2
            exit 1
        fi
        auto_start="1"
        auto_start_delay_ms="${LS2K_AUTO_START_DELAY_MS:-200}"
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

# Stop any running board process first so binary upload does not fail with
# "Text file busy" when debug.sh copies the new runtime.
"${SCRIPT_DIR}/debug.sh" remote stop
"${SCRIPT_DIR}/debug.sh" build

exec env \
    LS2K_AUTO_START="${auto_start}" \
    LS2K_AUTO_START_DELAY_MS="${auto_start_delay_ms}" \
    CONFIRM_POWERED_START="${CONFIRM_POWERED_START:-0}" \
    "${SCRIPT_DIR}/debug.sh" remote restart normal
