# Included by mc_add_test in cmake/targets.cmake. Real games on the card through the session's headless scene and under
# fake input, and the built program, always --headless, --no-sound and with stdin from /dev/null, which it is given as its
# argument. It links compile/src/png to read the program's screenshots: an interface library because the test's target
# does not exist yet, and this brings the include root with the objects.
set(MC_TEST_ARGS $<TARGET_FILE:minecraft_c>)
set(MC_TEST_TIMEOUT 900)
add_library(mc_ui_atlas_game_png_decoder INTERFACE)
target_include_directories(mc_ui_atlas_game_png_decoder INTERFACE "${CMAKE_SOURCE_DIR}/compile/include")
target_link_libraries(mc_ui_atlas_game_png_decoder INTERFACE $<TARGET_OBJECTS:mc_compile_lib>)
set(MC_TEST_LIBS mc_ui_atlas_game_png_decoder)
