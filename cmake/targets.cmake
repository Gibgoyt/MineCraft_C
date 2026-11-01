# Every target, found by path. A new directory is picked up with no edit here (CONFIGURE_DEPENDS re-runs
# the glob at build time), which is what lets parallel stages add modules without touching a shared list.
#
#   run/src/**/index.c       mc_run, with main; minecraft_c links all of it, tests link all but main
#   asm/src/**/index.asm     mc_asm
#   cuda/src/**/index.cu     mc_cuda
#   compile/src/**/index.c   a file with main is the tool mc_tool_<path>; the rest is mc_compile_lib, linked into
#                            every tool and every tests/compile/ test
#   tests/harness/index.c    linked into every test
#   tests/**/index.c         the ctest <path with / as _>, such as tests/run/world/section -> run_world_section.
#                            Not under tests/harness, tests/fuzz, tests/soak or any fixtures/ directory
#   tests/**/test.cmake      optional settings for the test next to it; see mc_add_test

# Stage 01.1. gcc, not g++ or nvcc, performs the final link, and it does not know where cudart lives.
if(NOT IS_DIRECTORY "${CMAKE_CUDA_COMPILER_TOOLKIT_LIBRARY_ROOT}/lib64")
  message(FATAL_ERROR "No CUDA lib64 under '${CMAKE_CUDA_COMPILER_TOOLKIT_LIBRARY_ROOT}'")
endif()
set(MC_CUDA_LIB_DIR "${CMAKE_CUDA_COMPILER_TOOLKIT_LIBRARY_ROOT}/lib64")

function(mc_link_game target)
  set_target_properties(${target} PROPERTIES LINKER_LANGUAGE C)
  target_link_directories(${target} PRIVATE "${MC_CUDA_LIB_DIR}")
  target_link_libraries(${target} PRIVATE cudart_static m)
endfunction()

# Any stage may add a dependency to this, such as the lint that has to pass before anything compiles.
add_custom_target(mc_prebuild)

# ---- Game sources ----------------------------------------------------------------------------------

file(GLOB_RECURSE mc_run_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/run/src/*/index.c")
file(GLOB_RECURSE mc_asm_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/asm/src/*/index.asm")
file(GLOB_RECURSE mc_cuda_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/cuda/src/*/index.cu")

# cmake/hello/ proves the three languages link while the game does not exist yet. Its main calls its other
# two parts, so all three stay until run/src/main/ (Stage 03.1) replaces it. After that a hello part remains
# only while its language has no module, because an object library must have a source and the toolchain
# should keep being exercised.
if(EXISTS "${CMAKE_SOURCE_DIR}/run/src/main/index.c")
  set(MC_HELLO_MAIN OFF)
else()
  set(MC_HELLO_MAIN ON)
  list(APPEND mc_run_sources "${CMAKE_SOURCE_DIR}/cmake/hello/main/index.c")
endif()
if(MC_HELLO_MAIN OR NOT mc_asm_sources)
  list(APPEND mc_asm_sources "${CMAKE_SOURCE_DIR}/cmake/hello/simd/index.asm")
endif()
if(MC_HELLO_MAIN OR NOT mc_cuda_sources)
  list(APPEND mc_cuda_sources "${CMAKE_SOURCE_DIR}/cmake/hello/gpu/index.cu")
endif()

add_library(mc_asm OBJECT ${mc_asm_sources})
target_include_directories(mc_asm PRIVATE "${CMAKE_SOURCE_DIR}/asm/include" "${MC_GENERATED_DIR}")

add_library(mc_cuda OBJECT ${mc_cuda_sources})
target_include_directories(mc_cuda PRIVATE "${CMAKE_SOURCE_DIR}/cuda/include" "${CMAKE_SOURCE_DIR}/run/include"
  "${MC_GENERATED_DIR}")

add_library(mc_run OBJECT ${mc_run_sources})
target_include_directories(mc_run PRIVATE "${CMAKE_SOURCE_DIR}/run/include" "${MC_GENERATED_DIR}")

add_dependencies(mc_asm mc_prebuild)
add_dependencies(mc_cuda mc_prebuild)
add_dependencies(mc_run mc_prebuild)

# CUDA needs ASan options set before any sanitized program starts; see cmake/sanitize/index.c.
set(mc_sanitize_objects)
if(MC_PRESET STREQUAL "sanitize")
  add_library(mc_sanitize OBJECT "${CMAKE_SOURCE_DIR}/cmake/sanitize/index.c")
  set(mc_sanitize_objects $<TARGET_OBJECTS:mc_sanitize>)
endif()

add_executable(minecraft_c
  $<TARGET_OBJECTS:mc_asm> $<TARGET_OBJECTS:mc_cuda> $<TARGET_OBJECTS:mc_run> ${mc_sanitize_objects})
mc_link_game(minecraft_c)

# Stage 01.1: the C++ runtime stays out only because of the settings above, and those lean on CMake internals.
add_custom_command(TARGET minecraft_c POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -DMC_BINARY=$<TARGET_FILE:minecraft_c> -DMC_READELF=${CMAKE_READELF}
    -DMC_PRESET=${MC_PRESET} -P "${CMAKE_SOURCE_DIR}/cmake/check_link.cmake"
  VERBATIM)

# Only the hello program may run under ctest. The real game opens the display, and tests never do.
if(MC_HELLO_MAIN)
  add_test(NAME minecraft_c_hello COMMAND minecraft_c)
  set_tests_properties(minecraft_c_hello PROPERTIES TIMEOUT 60)
endif()

# ---- Build-time tools ------------------------------------------------------------------------------

file(GLOB_RECURSE mc_compile_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/compile/src/*/index.c")
set(mc_compile_lib_sources)
set(mc_tool_sources)
foreach(mc_source IN LISTS mc_compile_sources)
  file(STRINGS "${mc_source}" mc_has_main REGEX "^int32_t main[ ]*\\(")
  if(mc_has_main)
    list(APPEND mc_tool_sources "${mc_source}")
  else()
    list(APPEND mc_compile_lib_sources "${mc_source}")
  endif()
endforeach()

set(mc_compile_includes "${CMAKE_SOURCE_DIR}/compile/include" "${CMAKE_SOURCE_DIR}/run/include" "${MC_GENERATED_DIR}")
set(mc_compile_lib_objects)
if(mc_compile_lib_sources)
  add_library(mc_compile_lib OBJECT ${mc_compile_lib_sources})
  target_include_directories(mc_compile_lib PRIVATE ${mc_compile_includes})
  set(mc_compile_lib_objects $<TARGET_OBJECTS:mc_compile_lib>)
endif()

foreach(mc_source IN LISTS mc_tool_sources)
  cmake_path(GET mc_source PARENT_PATH mc_dir)
  cmake_path(RELATIVE_PATH mc_dir BASE_DIRECTORY "${CMAKE_SOURCE_DIR}/compile/src" OUTPUT_VARIABLE mc_name)
  string(REPLACE "/" "_" mc_name "${mc_name}")
  add_executable(mc_tool_${mc_name} "${mc_source}" ${mc_compile_lib_objects})
  target_include_directories(mc_tool_${mc_name} PRIVATE ${mc_compile_includes})
  target_link_libraries(mc_tool_${mc_name} PRIVATE m)
  set_target_properties(mc_tool_${mc_name} PROPERTIES
    OUTPUT_NAME "${mc_name}" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tools")
endforeach()

# ---- Tests -----------------------------------------------------------------------------------------

set(mc_harness_objects)
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/harness/index.c")
  add_library(mc_test_harness OBJECT "${CMAKE_SOURCE_DIR}/tests/harness/index.c")
  target_include_directories(mc_test_harness PRIVATE "${CMAKE_SOURCE_DIR}/tests" "${CMAKE_SOURCE_DIR}/run/include"
    "${MC_GENERATED_DIR}")
  set(mc_harness_objects $<TARGET_OBJECTS:mc_test_harness>)
endif()

# Everything a game test needs except an entry point. The test's own main replaces the game's. The regex uses
# [.] because CMake turns every backslash in a source list into a slash, which would silently match nothing. The
# optional ./ because $<TARGET_OBJECTS> reads mc_run.dir/./cmake/hello/... while the Ninja file shows no such segment.
set(mc_game_objects_no_main
  $<TARGET_OBJECTS:mc_asm> $<TARGET_OBJECTS:mc_cuda> ${mc_sanitize_objects}
  "$<FILTER:$<TARGET_OBJECTS:mc_run>,EXCLUDE,mc_run[.]dir/([.]/)?(run/src|cmake/hello)/main/index[.]c[.]o$>")

# A test.cmake next to the test's index.c is included here, before the target exists. It may set:
#   MC_TEST_NAME     the ctest name, where the path-derived one does not fit
#   MC_TEST_ARGS     arguments; generator expressions such as $<TARGET_FILE:mc_tool_x> work
#   MC_TEST_LIBS     more libraries to link
#   MC_TEST_TIMEOUT  seconds, default 300
# It may also call add_test itself to register more runs of the same or another target.
function(mc_add_test source)
  cmake_path(GET source PARENT_PATH dir)
  cmake_path(RELATIVE_PATH dir BASE_DIRECTORY "${CMAKE_SOURCE_DIR}/tests" OUTPUT_VARIABLE rel)
  string(REPLACE "/" "_" MC_TEST_NAME "${rel}")
  string(REGEX MATCH "^[^/]+" group "${rel}")
  set(MC_TEST_ARGS)
  set(MC_TEST_LIBS)
  set(MC_TEST_TIMEOUT 300)
  if(EXISTS "${dir}/test.cmake")
    include("${dir}/test.cmake")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dir}/test.cmake")
  endif()

  set(target mc_test_${MC_TEST_NAME})
  if(group STREQUAL "compile")
    add_executable(${target} "${source}" ${mc_compile_lib_objects} ${mc_harness_objects})
    target_include_directories(${target} PRIVATE ${mc_compile_includes} "${CMAKE_SOURCE_DIR}/tests")
    target_link_libraries(${target} PRIVATE m)
  else()
    add_executable(${target} "${source}" ${mc_game_objects_no_main} ${mc_harness_objects})
    target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/run/include" "${MC_GENERATED_DIR}"
      "${CMAKE_SOURCE_DIR}/tests")
    mc_link_game(${target})
  endif()
  if(MC_TEST_LIBS)
    target_link_libraries(${target} PRIVATE ${MC_TEST_LIBS})
  endif()
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME "${MC_TEST_NAME}" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests")
  add_test(NAME ${MC_TEST_NAME} COMMAND ${target} ${MC_TEST_ARGS} WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(${MC_TEST_NAME} PROPERTIES TIMEOUT ${MC_TEST_TIMEOUT})
endfunction()

file(GLOB_RECURSE mc_test_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*/index.c")
file(GLOB_RECURSE mc_test_settings CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*/test.cmake")
foreach(mc_source IN LISTS mc_test_sources)
  if(NOT mc_source MATCHES "/tests/(harness|fuzz|soak)/" AND NOT mc_source MATCHES "/fixtures/")
    mc_add_test("${mc_source}")
  endif()
endforeach()
