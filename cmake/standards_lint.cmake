# Wires compile/src/standards_lint into the build. Included after cmake/targets.cmake, because it needs every target.
#
# The tool itself has to exist first, so mc_tool_standards_lint and mc_compile_lib (linked into every tool) are the two
# targets that compile before the lint has run. A breach in them still fails the build; it just fails one step later.

# Runs on every build: it is quick, and it has no inputs Ninja could know about, since it walks the whole tree.
add_custom_target(mc_standards_lint
  COMMAND mc_tool_standards_lint --root "${CMAKE_SOURCE_DIR}"
  COMMENT "Standards lint"
  VERBATIM)
add_dependencies(mc_prebuild mc_standards_lint)

# Everything else waits for the lint. mc_asm, mc_cuda and mc_run already depend on mc_prebuild.
get_property(mc_all_targets DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
set(mc_callgraph_targets)
foreach(mc_target IN LISTS mc_all_targets)
  if(mc_target MATCHES "^mc_(tool|test|bench)_" AND NOT mc_target STREQUAL "mc_tool_standards_lint")
    add_dependencies(${mc_target} mc_prebuild)
  endif()
  # What compiled C goes into the recursion check. The object libraries a program links are listed themselves.
  if(mc_target MATCHES "^mc_(tool_.*|test_.*|bench_.*|run|compile_lib|sanitize)$")
    list(APPEND mc_callgraph_targets ${mc_target})
  endif()
endforeach()

set(mc_lint_test_args --root "${CMAKE_SOURCE_DIR}")

# Recursion: plans/CODING_STANDARD.md section 10. GCC writes one .ci file per object while compiling. Under -flto it
# writes none at compile time, so release and profile compile every C source of these targets once more, as object
# libraries that nothing links, only to get the .ci files. The preset's defines stay, so what NDEBUG selects is checked.
set(mc_object_lists)
foreach(mc_target IN LISTS mc_callgraph_targets)
  set(mc_callgraph_target ${mc_target})
  if(NOT MC_C_CALLGRAPH)
    get_target_property(mc_sources ${mc_target} SOURCES)
    list(FILTER mc_sources INCLUDE REGEX "[.]c$")
    if(NOT mc_sources)
      continue()
    endif()
    get_target_property(mc_includes ${mc_target} INCLUDE_DIRECTORIES)
    set(mc_callgraph_target ${mc_target}_callgraph)
    add_library(${mc_callgraph_target} OBJECT ${mc_sources})
    target_include_directories(${mc_callgraph_target} PRIVATE ${mc_includes})
    mc_callgraph_compile_options(${mc_callgraph_target})
    add_dependencies(${mc_callgraph_target} mc_prebuild mc_struct_offsets) # a source may include what that generates
  endif()
  list(APPEND mc_callgraph_objects ${mc_callgraph_target})
  string(APPEND mc_object_lists "$<JOIN:$<FILTER:$<TARGET_OBJECTS:${mc_callgraph_target}>,INCLUDE,[.]c[.]o$>,\n>\n")
endforeach()
set(mc_callgraph_list "${CMAKE_BINARY_DIR}/standards_lint_objects.list")
file(GENERATE OUTPUT "${mc_callgraph_list}" CONTENT "${mc_object_lists}")
add_custom_target(mc_recursion_check ALL
  COMMAND mc_tool_standards_lint --callgraph "${mc_callgraph_list}"
  COMMENT "Recursion check"
  VERBATIM)
add_dependencies(mc_recursion_check ${mc_callgraph_objects})
list(APPEND mc_lint_test_args --callgraph "${mc_callgraph_list}")

# The same checks as the build, on demand. Its partner, standards_lint_fixtures, comes from tests/compile/standards_lint/.
add_test(NAME standards_lint COMMAND mc_tool_standards_lint ${mc_lint_test_args})
set_tests_properties(standards_lint PROPERTIES TIMEOUT 120)

# plans/CODING_STANDARD.md section 6: clang-format runs from the config in the repository root. A test, not a build step,
# so a reformat in progress does not stop a build, and ctest is what says it is finished.
find_program(MC_CLANG_FORMAT clang-format)
add_test(NAME clang_format
  COMMAND "${CMAKE_COMMAND}" "-DMC_CLANG_FORMAT=${MC_CLANG_FORMAT}" "-DMC_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    -P "${CMAKE_SOURCE_DIR}/cmake/check_format.cmake")
set_tests_properties(clang_format PROPERTIES TIMEOUT 120)
