# Included by mc_add_test in cmake/targets.cmake. Named as the other render_feature_* tests are: plan 004 phase 08's weather in
# the renderer. The test runs the built program, always --headless and with stdin from /dev/null, and links compile/src/png to
# read its golden pictures: an interface library because the test's target does not exist yet, and this brings the include root.
set(MC_TEST_NAME render_feature_weather)
set(MC_TEST_ARGS $<TARGET_FILE:minecraft_c>)
set(MC_TEST_TIMEOUT 280)
add_library(mc_render_feature_weather_png_decoder INTERFACE)
target_include_directories(mc_render_feature_weather_png_decoder INTERFACE "${CMAKE_SOURCE_DIR}/compile/include")
target_link_libraries(mc_render_feature_weather_png_decoder INTERFACE $<TARGET_OBJECTS:mc_compile_lib>)
set(MC_TEST_LIBS mc_render_feature_weather_png_decoder)
