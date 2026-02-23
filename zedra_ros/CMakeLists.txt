cmake_minimum_required(VERSION 3.16)
project(zedra_ros)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rosidl_default_generators REQUIRED)
if(NOT TARGET zedra_core)
  find_package(zedra_core REQUIRED)
endif()

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/ZedraEvent.msg"
  "msg/SnapshotMeta.msg"
)

add_executable(zedra_ros_node
  src/node.cpp
  src/zedra_bridge_node.cpp
)
target_include_directories(zedra_ros_node PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
rosidl_get_typesupport_target(cpp_typesupport_target "${PROJECT_NAME}" "rosidl_typesupport_cpp")
target_link_libraries(zedra_ros_node PUBLIC
  rclcpp::rclcpp
  zedra_core::zedra_core
  "${cpp_typesupport_target}"
)

install(TARGETS
  zedra_ros_node
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_test REQUIRED)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  ament_add_test(test_replay_determinism
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/test_replay_determinism.py
    TIMEOUT 120
  )
endif()

ament_package()
