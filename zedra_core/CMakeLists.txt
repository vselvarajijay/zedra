find_package(ament_cmake QUIET)

add_library(zedra_core
  src/lock_free_queue.cpp
  src/state.cpp
  src/reducer.cpp
)
target_include_directories(zedra_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if(ament_cmake_FOUND)
  install(TARGETS zedra_core
    EXPORT zedra_coreTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
  )
  install(DIRECTORY include/
    DESTINATION include
  )
  ament_export_include_directories(include)
  ament_export_libraries(zedra_core)
  ament_export_targets(zedra_coreTargets HAS_LIBRARY_TARGET)
  ament_package()
else()
  add_subdirectory(tests)
endif()
