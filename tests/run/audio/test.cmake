# Included by mc_add_test in cmake/targets.cmake. The test also runs the built program, always --headless, with stdin from /dev/null and its sound in a file.
set(MC_TEST_ARGS $<TARGET_FILE:minecraft_c>)
set(MC_TEST_TIMEOUT 280)
