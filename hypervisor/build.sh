#!/usr/bin/env bash
# Build hypervisor_b.ko
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

MODULE_NAME="hypervisor_b"
KO="${MODULE_NAME}.ko"

echo "==> clean & build"
make clean
make

if [[ ! -f "$KO" ]]; then
	echo "error: $KO not found after build" >&2
	exit 1
fi

echo "==> built $KO"
