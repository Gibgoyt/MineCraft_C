# Included by mc_add_test in cmake/targets.cmake. The test runs the built program, always --headless and with stdin from
# /dev/null. It also links compile/src/png, the project's bounds-checked decoder, for an opinion on the files other than
# the encoder's own: an interface library because the test's target does not exist yet, and this brings the include root
# with the objects.
set(MC_TEST_ARGS $<TARGET_FILE:minecraft_c>)
add_library(mc_headless_png_decoder INTERFACE)
target_include_directories(mc_headless_png_decoder INTERFACE "${CMAKE_SOURCE_DIR}/compile/include")
target_link_libraries(mc_headless_png_decoder INTERFACE $<TARGET_OBJECTS:mc_compile_lib>)
set(MC_TEST_LIBS mc_headless_png_decoder)
