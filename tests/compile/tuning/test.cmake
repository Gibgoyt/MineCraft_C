# Included by mc_add_test. Hands the test the tool, the real compile/config/, what the build generated from it, and a place to
# write its own fixtures. It runs from the source root, so its grep for each define sees run/, cuda/ and bench/.
set(MC_TEST_ARGS
  "$<TARGET_FILE:mc_tool_tuning>"
  "${CMAKE_SOURCE_DIR}/compile/config"
  "${MC_GENERATED_DIR}/tuning"
  "${CMAKE_BINARY_DIR}/test_work/tuning")
