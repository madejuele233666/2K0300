#!/bin/bash
# This script is the entry point for building the 'new' workspace.
# It is intended to be called by build_all.sh or the 'new/user/build.sh' script.

# Ensure we are in the 'new/' directory
SCRIPT_DIR=$(cd $(dirname "$0") && pwd)
if [ "$(basename $SCRIPT_DIR)" != "new" ]; then
    echo "Error: This script must be run from the 'new/' directory."
    exit 1
fi

echo "Executing build for 'new' workspace..."

# Execute the user's build script, which handles the actual compilation and linking.
# This allows build_all.sh to discover this workspace via its 'user/build.sh'.
if [ -f "user/build.sh" ]; then
    ./user/build.sh
else
    echo "Error: 'user/build.sh' not found in the 'new/' directory. Cannot proceed with build."
    exit 1
fi

echo "'new' workspace build process completed."
