# tests/fuzz and tests/soak, which the discovery in cmake/targets.cmake leaves out. Stage 18.1. See tests/fuzz/README.md and tests/soak/README.md.

# ---- The soak: a program of every preset, run by hand, never by ctest --------------------------------------------------

add_executable(mc_soak "${CMAKE_SOURCE_DIR}/tests/soak/index.c" ${mc_game_objects_no_main} ${mc_harness_objects})
target_include_directories(mc_soak PRIVATE "${CMAKE_SOURCE_DIR}/run/include" "${MC_GENERATED_DIR}"
  "${CMAKE_SOURCE_DIR}/tests")
mc_link_game(mc_soak)
set_target_properties(mc_soak PROPERTIES OUTPUT_NAME "soak")
# It runs the program next to it.
add_dependencies(mc_soak minecraft_c)

# ---- The fuzzers: only the sanitize preset, as what they look for takes ASan and UBSan to see ---------------------------

if(MC_PRESET STREQUAL "sanitize")
  add_library(mc_fuzz_support OBJECT "${CMAKE_SOURCE_DIR}/tests/fuzz/support/index.c")
  target_include_directories(mc_fuzz_support PRIVATE "${CMAKE_SOURCE_DIR}/tests" "${CMAKE_SOURCE_DIR}/run/include"
    "${MC_GENERATED_DIR}")

  # What is under test is compiled again, with the coverage calls, in place of its usual object.
  add_library(mc_fuzz_parsers OBJECT
    "${CMAKE_SOURCE_DIR}/compile/src/yaml/index.c"
    "${CMAKE_SOURCE_DIR}/compile/src/png/index.c"
    "${CMAKE_SOURCE_DIR}/compile/src/inflate/index.c")
  target_include_directories(mc_fuzz_parsers PRIVATE ${mc_compile_includes})
  mc_fuzz_compile_options(mc_fuzz_parsers)
  add_dependencies(mc_fuzz_parsers mc_prebuild)

  add_library(mc_fuzz_save_code OBJECT
    "${CMAKE_SOURCE_DIR}/run/src/save/world/index.c"
    "${CMAKE_SOURCE_DIR}/run/src/save/world/record/index.c"
    "${CMAKE_SOURCE_DIR}/run/src/save/names/index.c"
    "${CMAKE_SOURCE_DIR}/run/src/save/state/index.c"
    "${CMAKE_SOURCE_DIR}/run/src/save/state/translate/index.c"
    "${CMAKE_SOURCE_DIR}/run/src/ui/menu/catalog/index.c"
    "${CMAKE_SOURCE_DIR}/run/src/settings/persist/index.c")
  target_include_directories(mc_fuzz_save_code PRIVATE "${CMAKE_SOURCE_DIR}/run/include" "${MC_GENERATED_DIR}")
  mc_fuzz_compile_options(mc_fuzz_save_code)
  add_dependencies(mc_fuzz_save_code mc_run)

  set(mc_fuzz_game_objects
    $<TARGET_OBJECTS:mc_asm> $<TARGET_OBJECTS:mc_cuda> ${mc_sanitize_objects}
    "$<FILTER:$<TARGET_OBJECTS:mc_run>,EXCLUDE,mc_run[.]dir/([.]/)?run/src/(main|save/names|save/world|save/world/record|save/state|save/state/translate|ui/menu/catalog|settings/persist)/index[.]c[.]o$>")

  function(mc_fuzz_add name)
    set(target mc_fuzz_${name})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "fuzz_${name}")
    target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/tests" "${CMAKE_SOURCE_DIR}/run/include"
      "${CMAKE_SOURCE_DIR}/compile/include" "${MC_GENERATED_DIR}")
    # Short enough for ctest, and long enough to have gone through every seed and well past them. By hand it takes --seconds.
    add_test(NAME fuzz_${name} COMMAND ${target} --seconds 20 --crash-dir "${CMAKE_BINARY_DIR}/fuzz_crashes"
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(fuzz_${name} PROPERTIES TIMEOUT 300)
  endfunction()

  add_executable(mc_fuzz_yaml "${CMAKE_SOURCE_DIR}/tests/fuzz/yaml/index.c" $<TARGET_OBJECTS:mc_fuzz_support>
    $<TARGET_OBJECTS:mc_fuzz_parsers> ${mc_harness_objects})
  target_link_libraries(mc_fuzz_yaml PRIVATE m)
  mc_fuzz_add(yaml)

  add_executable(mc_fuzz_png "${CMAKE_SOURCE_DIR}/tests/fuzz/png/index.c" $<TARGET_OBJECTS:mc_fuzz_support>
    $<TARGET_OBJECTS:mc_fuzz_parsers> ${mc_harness_objects})
  target_link_libraries(mc_fuzz_png PRIVATE m)
  mc_fuzz_add(png)

  add_executable(mc_fuzz_save "${CMAKE_SOURCE_DIR}/tests/fuzz/save/index.c" $<TARGET_OBJECTS:mc_fuzz_support>
    $<TARGET_OBJECTS:mc_fuzz_save_code> ${mc_fuzz_game_objects} ${mc_harness_objects})
  mc_link_game(mc_fuzz_save)
  mc_fuzz_add(save)

  # Whether all of that would notice anything. The file itself is what is under test, so it has the coverage calls.
  add_executable(mc_fuzz_planted "${CMAKE_SOURCE_DIR}/tests/fuzz/planted/index.c" $<TARGET_OBJECTS:mc_fuzz_support>
    ${mc_harness_objects})
  mc_fuzz_compile_options(mc_fuzz_planted)
  target_link_libraries(mc_fuzz_planted PRIVATE m)
  set_target_properties(mc_fuzz_planted PROPERTIES OUTPUT_NAME "fuzz_planted")
  target_include_directories(mc_fuzz_planted PRIVATE "${CMAKE_SOURCE_DIR}/tests" "${CMAKE_SOURCE_DIR}/run/include"
    "${MC_GENERATED_DIR}")
  add_test(NAME fuzz_planted COMMAND mc_fuzz_planted WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(fuzz_planted PROPERTIES TIMEOUT 300)
endif()
