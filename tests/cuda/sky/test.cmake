# Included by mc_add_test in cmake/targets.cmake. The name is the phase's: every effect of phase 9 has a ctest render_feature_*.
# The test runs the built program, always --headless and with stdin from /dev/null, and links compile/src/png to read its golden
# pictures: an interface library because the test's target does not exist yet, and this brings the include root with the objects.
set(MC_TEST_NAME render_feature_sky)
set(MC_TEST_ARGS $<TARGET_FILE:minecraft_c>)
set(MC_TEST_TIMEOUT 280)
add_library(mc_render_feature_sky_png_decoder INTERFACE)
target_include_directories(mc_render_feature_sky_png_decoder INTERFACE "${CMAKE_SOURCE_DIR}/compile/include")
target_link_libraries(mc_render_feature_sky_png_decoder INTERFACE $<TARGET_OBJECTS:mc_compile_lib>)
set(MC_TEST_LIBS mc_render_feature_sky_png_decoder)
