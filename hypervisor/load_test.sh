#!/usr/bin/env bash
# Full cycle: build, load, show log, unload
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$SCRIPT_DIR/build.sh"
"$SCRIPT_DIR/run.sh"
"$SCRIPT_DIR/stop.sh"
