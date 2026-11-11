#!/bin/bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

DEVICE="${1:-x4}"
echo "=== Building ${DEVICE} Desktop Simulator ==="
make -f simulator/Makefile DEVICE="$DEVICE" -j4

echo "=== Launching Simulator (${DEVICE}) ==="
./simulator/${DEVICE}_simulator
