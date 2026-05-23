#!/usr/bin/env bash
# Build hypervisor_b.ko, load it, show kernel log, then unload.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

MODULE_NAME="hypervisor_b"
KO="${MODULE_NAME}.ko"
DMESG_TAG="hypervisor_b:"

show_module_log() {
	local label="$1"
	echo "==> kernel log: $label"
	if ! sudo dmesg | grep "$DMESG_TAG"; then
		echo "(no lines matching $DMESG_TAG)"
	fi
}

clear_kernel_log() {
	# -C: clear only (preferred). -c: read then clear (older fallback).
	if sudo dmesg -C >/dev/null 2>&1; then
		return 0
	fi
	sudo dmesg -c >/dev/null 2>&1 || true
}

echo "==> clean & build"
make clean
make

if [[ ! -f "$KO" ]]; then
	echo "error: $KO not found after build" >&2
	exit 1
fi

echo "==> clear kernel log buffer (only this run's messages)"
clear_kernel_log

echo "==> insmod $KO"
if ! sudo insmod "$KO"; then
	echo "error: insmod failed" >&2
	show_module_log "after failed insmod"
	exit 1
fi

show_module_log "after insmod"

echo "==> rmmod $MODULE_NAME"
sudo rmmod "$MODULE_NAME"

show_module_log "after rmmod (load + unload)"

echo "==> done"
