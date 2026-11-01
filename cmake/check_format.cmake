# Run by ctest clang_format: plans/CODING_STANDARD.md section 6. Fails if any C or CUDA file differs from what .clang-format
# would write. Fixtures break rules on purpose, so they are left out. Fix a failure with: clang-format -i <file>
#   cmake -DMC_CLANG_FORMAT=<path> -DMC_SOURCE_DIR=<repository> -P cmake/check_format.cmake
if(NOT MC_CLANG_FORMAT)
  message(FATAL_ERROR "check_format: clang-format was not found when this build was configured")
endif()

set(mc_globs)
foreach(mc_root run compile cuda tests bench cmake)
  foreach(mc_extension c h cu hu)
    list(APPEND mc_globs "${MC_SOURCE_DIR}/${mc_root}/*.${mc_extension}")
  endforeach()
endforeach()
file(GLOB_RECURSE mc_files ${mc_globs})
list(FILTER mc_files EXCLUDE REGEX "/fixtures/")
if(NOT mc_files)
  message(FATAL_ERROR "check_format: found no files under ${MC_SOURCE_DIR}. Is this path right?")
endif()

execute_process(
  COMMAND "${MC_CLANG_FORMAT}" --dry-run --Werror "--style=file:${MC_SOURCE_DIR}/.clang-format" ${mc_files}
  RESULT_VARIABLE mc_result
  ERROR_VARIABLE mc_output
  OUTPUT_VARIABLE mc_output)
if(NOT mc_result EQUAL 0)
  message(FATAL_ERROR "check_format: clang-format reports differences:\n${mc_output}")
endif()
list(LENGTH mc_files mc_count)
message(STATUS "check_format: ${mc_count} files match .clang-format")
