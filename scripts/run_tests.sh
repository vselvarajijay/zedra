#!/usr/bin/env bash
# Build and run tests.
# - Default / full: zedra_core tests (standalone, no ROS/ament).
#   Usage: ./run_tests.sh [full]   # full = run full suite
# - ros: zedra_ros tests (requires ROS 2 sourced, e.g. source /opt/ros/<distro>/setup.bash).
#   Usage: ./run_tests.sh ros
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [[ "${1:-}" == "ros" ]]; then
  if [[ -z "${ROS_DISTRO:-}" ]]; then
    echo "run_tests.sh ros: ROS 2 must be sourced (e.g. source /opt/ros/<distro>/setup.bash). ROS_DISTRO is not set." >&2
    exit 1
  fi
  ROS_WS="${ROOT}/build_ros_ws"
  mkdir -p "${ROS_WS}/src"
  for pkg in zedra_core zedra_ros; do
    if [[ ! -e "${ROS_WS}/src/${pkg}" ]]; then
      ln -s "${ROOT}/${pkg}" "${ROS_WS}/src/${pkg}"
    fi
  done
  cd "${ROS_WS}"
  source "/opt/ros/${ROS_DISTRO}/setup.bash"
  colcon build --packages-up-to zedra_ros
  source install/setup.bash
  colcon test --packages-select zedra_ros --event-handlers console_direct+
  colcon test-result --verbose
  exit 0
fi

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

# CLI smoke tests: demo (determinism) and replay (fixture + expect-hash)
CLI="${BUILD_DIR}/zedra_cli/zedra_cli"
WRITE_FIXTURE="${BUILD_DIR}/zedra_cli/write_fixture"
FIXTURES_DIR="${ROOT}/zedra_cli/fixtures"
if [[ -x "$CLI" && -x "$WRITE_FIXTURE" ]]; then
  H1=$( "$CLI" demo 2>/dev/null | sed -n 's/^version=.* hash=0x\([0-9a-fA-F]*\).*/\1/p' )
  H2=$( "$CLI" demo 2>/dev/null | sed -n 's/^version=.* hash=0x\([0-9a-fA-F]*\).*/\1/p' )
  if [[ "$H1" != "$H2" || -z "$H1" ]]; then
    echo "zedra_cli: demo determinism check failed (hash1=$H1 hash2=$H2)" >&2
    exit 1
  fi
  mkdir -p "$FIXTURES_DIR"
  "$WRITE_FIXTURE" > "${FIXTURES_DIR}/sample.bin"
  "$CLI" replay "${FIXTURES_DIR}/sample.bin" >/dev/null 2>&1 || { echo "zedra_cli: replay failed" >&2; exit 1; }
  "$CLI" replay --expect-hash "0x${H1}" "${FIXTURES_DIR}/sample.bin" >/dev/null 2>&1 || { echo "zedra_cli: replay --expect-hash failed" >&2; exit 1; }
  "$CLI" replay --expect-hash 0x0 "${FIXTURES_DIR}/sample.bin" 2>/dev/null && { echo "zedra_cli: expect-hash mismatch should exit non-zero" >&2; exit 1; }
  echo "zedra_cli smoke tests OK"
fi
