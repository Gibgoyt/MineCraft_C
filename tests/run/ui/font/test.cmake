# Included by mc_add_test in cmake/targets.cmake. The test also runs the built program, always --headless and with stdin from
# /dev/null.
set(MC_TEST_ARGS $<TARGET_FILE:minecraft_c>)
