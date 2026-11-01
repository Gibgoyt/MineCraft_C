# Root build file. Source lists do not live here: cmake/targets.cmake discovers modules by path, so
# parallel stages add directories without editing a shared list. See plans/REPO_STRUCTURE.md section 10.
cmake_minimum_required(VERSION 4.4)
project(minecraft_c LANGUAGES C ASM_NASM CUDA)

set(MC_PRESET "" CACHE STRING "The configure preset: debug, sanitize, release or profile")
set(MC_PRESETS debug sanitize release profile)
if(NOT MC_PRESET IN_LIST MC_PRESETS)
  message(FATAL_ERROR "MC_PRESET is '${MC_PRESET}'. Configure with: cmake --preset debug|sanitize|release|profile")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(MC_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${MC_GENERATED_DIR}")

enable_testing()

# Written by later stages (02.3 and 07.1). Optional, so those stages do not have to edit this file.
include(cmake/machine.cmake OPTIONAL)
include(cmake/flags.cmake)
include(cmake/generated.cmake OPTIONAL)
include(cmake/targets.cmake)
include(cmake/standards_lint.cmake)
