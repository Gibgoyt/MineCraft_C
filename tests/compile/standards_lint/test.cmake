# Included by mc_add_test in cmake/targets.cmake. The fixtures test judges the same flags the build uses, so a flag
# dropped from cmake/flags.cmake fails it. -fcallgraph-info comes from the test itself where it is needed: the flag
# writes a .ci file next to whatever object is being compiled. The ctest standards_lint, which lints the repository, is
# registered in cmake/standards_lint.cmake.
set(MC_TEST_NAME standards_lint_fixtures)
set(mc_fixture_tmp "${CMAKE_BINARY_DIR}/test_tmp/standards_lint_fixtures")
file(MAKE_DIRECTORY "${mc_fixture_tmp}")
set(mc_fixture_flags ${MC_C_FLAGS})
list(REMOVE_ITEM mc_fixture_flags -fcallgraph-info=su)
set(MC_TEST_ARGS
  --tool $<TARGET_FILE:mc_tool_standards_lint>
  --fixtures "${CMAKE_CURRENT_LIST_DIR}/fixtures"
  --tmp "${mc_fixture_tmp}"
  --cc "${CMAKE_C_COMPILER}"
  -- ${mc_fixture_flags} -D_GNU_SOURCE=1)
set(MC_TEST_TIMEOUT 280)
