# Included by mc_add_test in cmake/targets.cmake. The test also runs the built program, always --headless and with stdin from /dev/null.
set(MC_TEST_ARGS $<TARGET_FILE:minecraft_c>)
set(MC_TEST_TIMEOUT 300)
# It holds the waits between pictures to within two frames of each other, which another test on the card moves.
set(MC_TEST_RUN_SERIAL TRUE)
