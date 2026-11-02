# Included by mc_add_test in cmake/targets.cmake. The test runs the same NASM the build uses.
set(mc_conventions_tmp "${CMAKE_BINARY_DIR}/test_tmp/asm_conventions")
file(MAKE_DIRECTORY "${mc_conventions_tmp}")
set(MC_TEST_ARGS
  "${CMAKE_ASM_NASM_COMPILER}"
  "${CMAKE_SOURCE_DIR}/asm/include"
  "${MC_GENERATED_DIR}"
  "${mc_conventions_tmp}")
