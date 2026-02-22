add_library(zedra_core
  src/lock_free_queue.cpp
  src/state.cpp
  src/reducer.cpp
)
target_include_directories(zedra_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_subdirectory(tests)
