# Included by mc_add_test in cmake/targets.cmake. Hands the test the tool, the committed sounds, the blob the build made from
# them, and a place to build its own small trees in.
set(MC_TEST_ARGS
  "$<TARGET_FILE:mc_tool_sound_pack>"
  "${CMAKE_SOURCE_DIR}/sounds"
  "${MC_GENERATED_DIR}/sounds/index.mcsb"
  "${CMAKE_BINARY_DIR}/test_work/sound_pack")
