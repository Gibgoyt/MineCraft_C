# Fails the build if the binary depends on a library the coding standard does not allow, or has an executable
# stack. Stage 01.1 measured what a correct link needs. Run by CMake after linking minecraft_c:
#   cmake -DMC_BINARY=<path> -DMC_READELF=<readelf> -DMC_PRESET=<preset> -P cmake/check_link.cmake
#
# A list of allowed names rather than a ban on libstdc++, so any other library that sneaks in fails as well.
set(mc_allowed libc.so.6 libm.so.6 ld-linux-x86-64.so.2)
if(MC_PRESET STREQUAL "tsan")
  list(APPEND mc_allowed libtsan.so.2)
endif()
if(MC_PRESET STREQUAL "sanitize")
  list(APPEND mc_allowed libasan.so.8 libubsan.so.1)
endif()

execute_process(COMMAND "${MC_READELF}" -dW "${MC_BINARY}" OUTPUT_VARIABLE mc_dynamic RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
  message(FATAL_ERROR "check_link: readelf -d failed on ${MC_BINARY}")
endif()
string(REGEX MATCHALL "\\(NEEDED\\)[^\n]*\\[[^]\n]+\\]" mc_lines "${mc_dynamic}")
if(NOT mc_lines)
  message(FATAL_ERROR "check_link: no NEEDED entries in ${MC_BINARY}. Is this parser still right?")
endif()
foreach(mc_line IN LISTS mc_lines)
  string(REGEX REPLACE ".*\\[(.*)\\]" "\\1" mc_name "${mc_line}")
  if(NOT mc_name IN_LIST mc_allowed)
    message(FATAL_ERROR "check_link: ${MC_BINARY} needs ${mc_name}. Allowed: ${mc_allowed}. "
      "See plans/CODING_STANDARD.md sections 1 and 9.")
  endif()
endforeach()

execute_process(COMMAND "${MC_READELF}" -lW "${MC_BINARY}" OUTPUT_VARIABLE mc_segments RESULT_VARIABLE mc_rc)
if(NOT mc_rc EQUAL 0)
  message(FATAL_ERROR "check_link: readelf -l failed on ${MC_BINARY}")
endif()
string(REGEX MATCH "GNU_STACK[^\n]*" mc_stack "${mc_segments}")
if(NOT mc_stack MATCHES " RW  ")
  message(FATAL_ERROR "check_link: ${MC_BINARY} stack is not plain RW: '${mc_stack}'")
endif()
