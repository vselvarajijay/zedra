add_library(zedra_core
  src/lock_free_queue.cpp
)
target_include_directories(zedra_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_subdirectory(tests)
