# Included by mc_add_test in cmake/targets.cmake. The test also runs the built program, always --headless and with stdin from
# /dev/null, and its traces go in a scratch directory inside the build tree.
set(mc_profiler_tmp "${CMAKE_BINARY_DIR}/test_tmp/run_profiler")
file(MAKE_DIRECTORY "${mc_profiler_tmp}")
set(MC_TEST_ARGS $<TARGET_FILE:minecraft_c> "${mc_profiler_tmp}")
set(MC_TEST_TIMEOUT 280)
