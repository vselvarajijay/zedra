#!/usr/bin/env bash
# Build the zedra Docker image and run zedra_ros tests inside the container.
# Usage: ./scripts/docker_test.sh [image_name]
# Default image name: zedra
#
# Builds linux/amd64 only (OSRF ROS Kilted has no arm64 image). On ARM Macs the
# container runs under Rosetta; the replay determinism test is skipped to avoid
# middleware bad_alloc (corrupted payload size on deserialize). CI on amd64 runs all tests.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${1:-zedra}"
PLATFORM="linux/amd64"

# On ARM host, skip the replay determinism test (avoids Rosetta deserialization bug).
case "$(uname -m)" in
  arm64|aarch64) DOCKER_ENV="-e SKIP_REPLAY_DETERMINISM=1" ;;
  *)             DOCKER_ENV="" ;;
esac

echo "Building Docker image: ${IMAGE} (platform ${PLATFORM})..."
docker build --platform "${PLATFORM}" -t "${IMAGE}" "${ROOT}"

echo "Running zedra_ros tests inside container..."
docker run --rm --platform "${PLATFORM}" ${DOCKER_ENV} "${IMAGE}" bash -c \
  "source /opt/ros/kilted/setup.bash && \
   source /root/ros2_ws/install/setup.bash && \
   colcon test --packages-select zedra_ros --event-handlers console_direct+ && \
   colcon test-result --verbose"

echo "Done: build and tests completed."
