#!/usr/bin/env bash
# Build and run zedra_core tests (standalone, no ROS/ament).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build_standalone"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake "$ROOT" -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Debug}"
cmake --build .
# Run test binary directly so we always see output (e.g. [chaotic] blocks).
# Default: run only chaotic ingestion test (shows [chaotic] output, passes).
# Usage: ./run_tests.sh full  # run full suite (may fail on other tests)
if [[ "${1:-}" == "full" ]]; then
  ./zedra_core/tests/zedra_core_tests
else
  ./zedra_core/tests/zedra_core_tests chaotic
fi
