# Included by mc_add_test in cmake/targets.cmake. The made-up /sys trees go in a scratch directory inside the build tree.
set(mc_thread_tmp "${CMAKE_BINARY_DIR}/test_tmp/run_thread")
file(MAKE_DIRECTORY "${mc_thread_tmp}")
set(MC_TEST_ARGS "${mc_thread_tmp}")
