#!/usr/bin/env bash
# Build and run the desktop DSP tests. Pass a soak length in seconds (default 60).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/test"
c++ -O2 -std=c++14 -Wall -Wextra -ffp-contract=off -o harness harness.cc -lm
mkdir -p out
./harness out "${1:-60}"
