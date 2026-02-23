# zedra_ros: ROS 2 Kilted image to build and run zedra_ros bridge node
FROM osrf/ros:kilted-desktop

# Build deps (base image has most; ensure colcon and toolchain)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    python3-colcon-common-extensions \
    python3-rosdep \
    && rm -rf /var/lib/apt/lists/*

# Workspace: colcon expects packages under src/
ARG WORKSPACE=/root/ros2_ws
RUN mkdir -p ${WORKSPACE}/src

# Copy only the two ROS packages (colcon resolves build order via deps)
COPY zedra_core ${WORKSPACE}/src/zedra_core
COPY zedra_ros  ${WORKSPACE}/src/zedra_ros

WORKDIR ${WORKSPACE}
# Use bash so ROS setup.bash (bash-only syntax) runs correctly; default RUN shell is sh
RUN bash -c "source /opt/ros/kilted/setup.bash && colcon build --packages-up-to zedra_ros"

# Run the bridge node
CMD ["bash", "-c", "source /opt/ros/kilted/setup.bash && source install/setup.bash && exec ros2 run zedra_ros zedra_ros_node"]
