#!/bin/bash
set -euo pipefail

WORK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "${SMOKE_LOCAL_ONLY:-0}" == "1" ]]; then
    exec "${WORK_DIR}/debug.sh" smoke local "$@"
fi

exec "${WORK_DIR}/debug.sh" smoke run "$@"
