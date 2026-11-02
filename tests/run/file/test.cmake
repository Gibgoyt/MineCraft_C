# Included by mc_add_test in cmake/targets.cmake. The test works in a scratch directory inside the build tree.
set(mc_file_tmp "${CMAKE_BINARY_DIR}/test_tmp/run_file")
file(MAKE_DIRECTORY "${mc_file_tmp}")
set(MC_TEST_ARGS "${mc_file_tmp}")
