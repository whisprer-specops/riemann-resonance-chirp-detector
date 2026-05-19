#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build_smoke
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc src/detector.cpp tools/smoke_detector.cpp -o build_smoke/smoke_detector
./build_smoke/smoke_detector
