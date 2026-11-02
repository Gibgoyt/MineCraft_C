# Included by mc_add_test in cmake/targets.cmake. Hands the test the tool, the committed manifest and textures, and a place
# to build its own small pack in. The real pack is never involved, so this runs with no network.
set(MC_TEST_ARGS
  "$<TARGET_FILE:mc_tool_texture_pack>"
  "${CMAKE_SOURCE_DIR}/textures/import.yaml"
  "${CMAKE_SOURCE_DIR}/textures"
  "${CMAKE_BINARY_DIR}/test_work/texture_pack")
