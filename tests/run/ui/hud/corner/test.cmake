# Included by mc_add_test in cmake/targets.cmake. Real games on the card with a climate over them, so minutes on debug, and it
# links compile/src/png to read its golden pictures: an interface library because the test's target does not exist yet, and
# this brings the include root with the objects.
set(MC_TEST_TIMEOUT 900)
add_library(mc_ui_hud_corner_png_decoder INTERFACE)
target_include_directories(mc_ui_hud_corner_png_decoder INTERFACE "${CMAKE_SOURCE_DIR}/compile/include")
target_link_libraries(mc_ui_hud_corner_png_decoder INTERFACE $<TARGET_OBJECTS:mc_compile_lib>)
set(MC_TEST_LIBS mc_ui_hud_corner_png_decoder)
