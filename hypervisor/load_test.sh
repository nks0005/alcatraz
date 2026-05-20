#!/usr/bin/env bash
# Build hypervisor_b.ko, load it, show kernel log, then unload.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

MODULE_NAME="hypervisor_b"
KO="${MODULE_NAME}.ko"
DMESG_TAG="hypervisor_b:"

echo "==> clean & build"
make clean
make

if [[ ! -f "$KO" ]]; then
	echo "error: $KO not found after build" >&2
	exit 1
fi

echo "==> insmod $KO"
sudo insmod "$KO"

echo "==> kernel log (last matching lines)"
sudo dmesg | grep "$DMESG_TAG" | tail -5 || true
sudo dmesg | tail -8

echo "==> rmmod $MODULE_NAME"
sudo rmmod "$MODULE_NAME"

echo "==> done"
