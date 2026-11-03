# Included by mc_add_test in cmake/targets.cmake. The test also runs the built program, always --headless and with stdin from
# /dev/null. It links compile/src/png to read the pictures that program writes: an interface library because the test's target does not
# exist yet, and this brings the include root with the objects.
set(MC_TEST_ARGS $<TARGET_FILE:minecraft_c>)
add_library(mc_settings_png_decoder INTERFACE)
target_include_directories(mc_settings_png_decoder INTERFACE "${CMAKE_SOURCE_DIR}/compile/include")
target_link_libraries(mc_settings_png_decoder INTERFACE $<TARGET_OBJECTS:mc_compile_lib>)
set(MC_TEST_LIBS mc_settings_png_decoder)
set(MC_TEST_TIMEOUT 300)
