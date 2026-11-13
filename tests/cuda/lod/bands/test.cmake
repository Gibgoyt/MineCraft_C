# Included by mc_add_test in cmake/targets.cmake. Plan 009 phase 04's near guarantee, error bound and seam, apart from render_feature_lod, which
# is at the size where a file is split. It runs on the card, and opens no display.
set(MC_TEST_NAME render_feature_lod_bands)
set(MC_TEST_TIMEOUT 590)
add_library(mc_render_feature_lod_bands_png INTERFACE)
target_include_directories(mc_render_feature_lod_bands_png INTERFACE "${CMAKE_SOURCE_DIR}/compile/include")
target_link_libraries(mc_render_feature_lod_bands_png INTERFACE $<TARGET_OBJECTS:mc_compile_lib>)
set(MC_TEST_LIBS mc_render_feature_lod_bands_png)
