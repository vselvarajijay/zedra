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
