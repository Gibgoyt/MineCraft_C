# Included by mc_add_test in cmake/targets.cmake. Hands the test the tool, the real config and textures, the tables the build
# made from them, and a place to build its own small trees in. cmake/targets.cmake adds the generated source to the target.
set(MC_TEST_ARGS
  "$<TARGET_FILE:mc_tool_game_tables>"
  "${CMAKE_SOURCE_DIR}/config"
  "${MC_TEXTURE_ROOT}"
  "${MC_GENERATED_DIR}/tables"
  "${CMAKE_BINARY_DIR}/test_work/game_tables")
