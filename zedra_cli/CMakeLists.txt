add_executable(zedra_cli src/main.cpp src/cli_options.cpp src/binary_log.cpp)
target_link_libraries(zedra_cli PRIVATE zedra_core)

# Fixture generator: writes binary event log to stdout (for tests/fixtures).
add_executable(write_fixture src/write_fixture.cpp)
# No link to zedra_core; standalone writer
