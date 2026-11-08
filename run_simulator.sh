#!/bin/bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "=== Building XTEINK X4 Desktop Simulator ==="
make -f simulator/Makefile -j4

echo "=== Launching Simulator ==="
./simulator/x4_simulator
