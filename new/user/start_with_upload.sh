#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Stop any running board process first so binary upload does not fail with
# "Text file busy" when debug.sh copies the new runtime.
"${SCRIPT_DIR}/debug.sh" remote stop
"${SCRIPT_DIR}/debug.sh" build

exec env \
    LS2K_AUTO_START="${LS2K_AUTO_START:-1}" \
    LS2K_AUTO_START_DELAY_MS="${LS2K_AUTO_START_DELAY_MS:-200}" \
    "${SCRIPT_DIR}/debug.sh" remote restart normal
