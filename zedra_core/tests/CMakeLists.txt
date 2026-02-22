enable_testing()
add_executable(zedra_core_tests
  main.cpp
  unit_event.cpp
  unit_state.cpp
  unit_lock_free_queue.cpp
  replay_test.cpp
  behavioral_guarantees.cpp
  example_smoke_test.cpp
  concurrent_test.cpp
)
target_link_libraries(zedra_core_tests PRIVATE zedra_core)

# Plain assertions: exit 0 on success, non-zero on failure.
target_compile_definitions(zedra_core_tests PRIVATE ZEDRA_SIMPLE_TEST=1)
add_test(NAME zedra_core_tests COMMAND zedra_core_tests)

option(ZEDRA_ENABLE_TSAN "Build tests with ThreadSanitizer" OFF)
if(ZEDRA_ENABLE_TSAN)
  target_compile_options(zedra_core_tests PRIVATE -fsanitize=thread)
  target_link_options(zedra_core_tests PRIVATE -fsanitize=thread)
endif()
