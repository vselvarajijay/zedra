cmake_minimum_required(VERSION 3.16)
project(Zedra LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(zedra_core)
add_subdirectory(zedra_cli)
add_subdirectory(examples)
find_package(ament_cmake QUIET)
if(ament_cmake_FOUND)
  add_subdirectory(zedra_ros)
endif()
