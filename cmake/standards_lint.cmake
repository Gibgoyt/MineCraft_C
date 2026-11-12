# Wires compile/src/standards_lint into the build. Included after cmake/targets.cmake, because it needs every target.
#
# The tool itself has to exist first, so mc_tool_standards_lint and mc_compile_lib (linked into every tool) compile before
# the lint has run, and so does mc_tool_tuning, whose headers every compile waits for. A breach in them still fails the
# build; it just fails one step later.

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
  # The tuning tool too, as mc_prebuild waits for what it makes: plan 009 phase 02.
  if(mc_target MATCHES "^mc_(tool|test|bench)_" AND NOT mc_target STREQUAL "mc_tool_standards_lint"
     AND NOT mc_target STREQUAL "mc_tool_tuning")
    add_dependencies(${mc_target} mc_prebuild)
  endif()
  # What compiled C goes into the recursion check. The object libraries a program links are listed themselves.
  if(mc_target MATCHES "^mc_(tool_.*|test_.*|bench_.*|fuzz_.*|run|compile_lib|sanitize|soak)$")
    list(APPEND mc_callgraph_targets ${mc_target})
  endif()
endforeach()

set(mc_lint_test_args --root "${CMAKE_SOURCE_DIR}")

# Recursion: plans/CODING_STANDARD.md section 10. GCC writes one .ci file per object while compiling. Only debug's own
# objects give the -O0 graph the check needs (cmake/flags.cmake says why), so every other preset compiles each C source
# of these targets once more, as object libraries that nothing links, only to get the .ci files. The preset's defines
# stay, so what NDEBUG selects is checked. The fuzzers exist only in sanitize; a target a preset does not have is simply
# not in the list.
set(mc_object_lists)
foreach(mc_target IN LISTS mc_callgraph_targets)
  set(mc_callgraph_target ${mc_target})
  if(NOT MC_C_CALLGRAPH)
    get_target_property(mc_sources ${mc_target} SOURCES)
    list(FILTER mc_sources INCLUDE REGEX "[.]c$")
    if(NOT mc_sources)
      continue()
    endif()
    set(mc_callgraph_target ${mc_target}_callgraph)
    add_library(${mc_callgraph_target} OBJECT ${mc_sources})
    # The generator expression, since it also brings what the target's libraries add: get_target_property would not.
    target_include_directories(${mc_callgraph_target} PRIVATE "$<TARGET_PROPERTY:${mc_target},INCLUDE_DIRECTORIES>")
    mc_callgraph_compile_options(${mc_callgraph_target})
    # A source may include what these generate. The real targets get the tables through mc_run's own custom command, which
    # these copies do not have, so a clean build used to fail here: Stage 08.2 issue 8 and the phase 8 gate.
    add_dependencies(${mc_callgraph_target} mc_prebuild mc_struct_offsets mc_game_tables)
  endif()
  list(APPEND mc_callgraph_objects ${mc_callgraph_target})
  string(APPEND mc_object_lists "$<JOIN:$<FILTER:$<TARGET_OBJECTS:${mc_callgraph_target}>,INCLUDE,[.]c[.]o$>,\n>\n")
endforeach()
set(mc_callgraph_list "${CMAKE_BINARY_DIR}/standards_lint_objects.list")
file(GENERATE OUTPUT "${mc_callgraph_list}" CONTENT "${mc_object_lists}")
# Calls through pointers: each one is either a job, which the tool follows, or reviewed in this list.
set(mc_indirect_calls "${CMAKE_SOURCE_DIR}/compile/src/standards_lint/indirect_calls.yaml")
add_custom_target(mc_recursion_check ALL
  COMMAND mc_tool_standards_lint --callgraph "${mc_callgraph_list}" --indirect-calls "${mc_indirect_calls}"
  COMMENT "Recursion check"
  VERBATIM)
add_dependencies(mc_recursion_check ${mc_callgraph_objects})
list(APPEND mc_lint_test_args --callgraph "${mc_callgraph_list}" --indirect-calls "${mc_indirect_calls}")

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
