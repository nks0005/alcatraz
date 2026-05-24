#!/usr/bin/env bash
# Unload hypervisor_b.ko and show kernel log
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

MODULE_NAME="hypervisor_b"
DMESG_TAG="hypervisor_b:"

show_module_log() {
	local label="$1"
	echo "==> kernel log: $label"
	if ! sudo dmesg | grep "$DMESG_TAG"; then
		echo "(no lines matching $DMESG_TAG)"
	fi
}

is_module_loaded() {
	LC_ALL=C lsmod | awk '$1 == "'"$MODULE_NAME"'" { found=1 } END { exit !found }'
}

if ! is_module_loaded; then
	echo "==> $MODULE_NAME is not loaded"
	exit 0
fi

echo "==> rmmod $MODULE_NAME"
sudo rmmod "$MODULE_NAME"

show_module_log "after rmmod"
echo "==> done (module unloaded)"
