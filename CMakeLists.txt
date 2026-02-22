cmake_minimum_required(VERSION 3.16)
project(Zedra LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(zedra_core)
add_subdirectory(zedra_ros)
add_subdirectory(zedra_cli)
add_subdirectory(examples)
