# Included by mc_add_test in cmake/targets.cmake. Hands the test the tool, the committed textures and their table, and a
# place to build its own small trees in.
set(MC_TEST_ARGS
  "$<TARGET_FILE:mc_tool_texture_vector>"
  "${CMAKE_SOURCE_DIR}/textures"
  "${CMAKE_SOURCE_DIR}/textures/vector.yaml"
  "${CMAKE_BINARY_DIR}/test_work/texture_vector")
