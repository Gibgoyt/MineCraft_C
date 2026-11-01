# Included by mc_add_test. Hands the test the tool, the real inputs and outputs, and every language's compiler, so it
# can prove each generated file is accepted by the program it was written for.
set(MC_TEST_ARGS
  "$<TARGET_FILE:mc_tool_machine_config>"
  "$<TARGET_FILE:mc_tool_machine_config_report>"
  "${CMAKE_SOURCE_DIR}/config/machine.yaml"
  "${MC_GENERATED_DIR}/machine"
  "${CMAKE_BINARY_DIR}/test_work/machine_config"
  "${CMAKE_C_COMPILER}"
  "${CMAKE_ASM_NASM_COMPILER}"
  "${CMAKE_CUDA_COMPILER}"
  "${CMAKE_COMMAND}")
