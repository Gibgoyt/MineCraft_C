# Included by mc_add_test. Hands the test the tool, somewhere to write, what the build generated, and the compilers that
# have to accept each output.
set(MC_TEST_ARGS
  "$<TARGET_FILE:mc_tool_struct_offsets>"
  "${CMAKE_BINARY_DIR}/test_work/struct_offsets"
  "${MC_GENERATED_DIR}/struct_offsets"
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_C_COMPILER}"
  "${CMAKE_ASM_NASM_COMPILER}")
