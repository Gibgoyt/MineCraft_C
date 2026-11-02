# Included by mc_add_test in cmake/targets.cmake. Hands the test both tools, and a place to build its own small trees in.
set(MC_TEST_ARGS
  "$<TARGET_FILE:mc_tool_texture_blob>"
  "$<TARGET_FILE:mc_tool_texture_vector>"
  "${CMAKE_BINARY_DIR}/test_work/texture_blob")
