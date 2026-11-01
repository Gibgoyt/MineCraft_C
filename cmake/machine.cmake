# Turns config/machine.yaml into build/<preset>/generated/machine/index.{h,hu,inc,cmake} and loads the CMake one.
# Included before cmake/flags.cmake, because -march comes from MC_CPU_ARCH_FLAG. Nothing else exists yet at this point,
# so the tool cannot be a build target: it is compiled and run right here, on every configure. The tool leaves an
# unchanged output untouched, so a configure with an unchanged yaml rebuilds nothing.

set(mc_machine_yaml "${CMAKE_SOURCE_DIR}/config/machine.yaml")
set(mc_machine_sources
  "${CMAKE_SOURCE_DIR}/compile/src/machine_config/index.c"
  "${CMAKE_SOURCE_DIR}/compile/src/yaml/index.c")
set(mc_machine_inputs ${mc_machine_yaml} ${mc_machine_sources}
  "${CMAKE_SOURCE_DIR}/compile/include/machine_config/index.h"
  "${CMAKE_SOURCE_DIR}/compile/include/yaml/index.h")
foreach(mc_input IN LISTS mc_machine_inputs)
  if(NOT EXISTS "${mc_input}")
    message(FATAL_ERROR "cmake/machine.cmake needs ${mc_input}, from Stage 02.2 or 02.3")
  endif()
endforeach()

# Ninja re-runs this configure when any of them changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${mc_machine_inputs})

# The standard's warnings, without -march: the flag this tool is about to produce cannot be an input to it. -O0 because it
# runs once on a 1 KiB file, and so that an optimizer-only warning stops at the build target, not at configure. The same
# source gets the full flags again as the build target mc_tool_machine_config, which the standards lint gates.
set(mc_machine_tool "${CMAKE_BINARY_DIR}/machine_tool/machine_config")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/machine_tool")
execute_process(
  COMMAND "${CMAKE_C_COMPILER}" -std=c23 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wdouble-promotion
    -Wshadow -Wvla -Wpadded -Werror -O0 -D_GNU_SOURCE=1 "-I${CMAKE_SOURCE_DIR}/compile/include" ${mc_machine_sources}
    -o "${mc_machine_tool}"
  TIMEOUT 120
  RESULT_VARIABLE mc_machine_result
  ERROR_VARIABLE mc_machine_output
  OUTPUT_VARIABLE mc_machine_output)
if(NOT mc_machine_result EQUAL 0)
  message(FATAL_ERROR "Could not compile the machine_config tool (${mc_machine_result}):\n${mc_machine_output}")
endif()

execute_process(
  COMMAND "${mc_machine_tool}" "${mc_machine_yaml}" "${MC_GENERATED_DIR}/machine"
  TIMEOUT 60
  RESULT_VARIABLE mc_machine_result
  ERROR_VARIABLE mc_machine_output
  OUTPUT_VARIABLE mc_machine_output)
if(NOT mc_machine_result EQUAL 0)
  message(FATAL_ERROR "config/machine.yaml was refused (${mc_machine_result}):\n${mc_machine_output}")
endif()
include("${MC_GENERATED_DIR}/machine/index.cmake")

# machine.yaml names the toolchain it was verified with, so a machine with an older one stops here and not in some
# strange compile error later.
if(CMAKE_VERSION VERSION_LESS MC_TOOLCHAIN_CMAKE_MIN)
  message(FATAL_ERROR "config/machine.yaml needs CMake ${MC_TOOLCHAIN_CMAKE_MIN}, found ${CMAKE_VERSION}")
endif()
if(CMAKE_C_COMPILER_VERSION VERSION_LESS MC_TOOLCHAIN_GCC_MIN)
  message(FATAL_ERROR "config/machine.yaml needs GCC ${MC_TOOLCHAIN_GCC_MIN}, found ${CMAKE_C_COMPILER_VERSION}")
endif()
if(CMAKE_CUDA_COMPILER_VERSION VERSION_LESS MC_TOOLCHAIN_NVCC_MIN)
  message(FATAL_ERROR "config/machine.yaml needs nvcc ${MC_TOOLCHAIN_NVCC_MIN}, found ${CMAKE_CUDA_COMPILER_VERSION}")
endif()
if(CMAKE_ASM_NASM_COMPILER_VERSION AND CMAKE_ASM_NASM_COMPILER_VERSION VERSION_LESS MC_TOOLCHAIN_NASM_MIN)
  message(FATAL_ERROR
    "config/machine.yaml needs NASM ${MC_TOOLCHAIN_NASM_MIN}, found ${CMAKE_ASM_NASM_COMPILER_VERSION}")
endif()
