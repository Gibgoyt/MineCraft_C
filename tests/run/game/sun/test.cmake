# Included by mc_add_test in cmake/targets.cmake. The test also runs the built program, always --headless, --no-sound and with
# stdin from /dev/null, for --day-of-year. Worlds opened on the card, and bodies moved tens of kilometres north.
set(MC_TEST_ARGS $<TARGET_FILE:minecraft_c>)
set(MC_TEST_TIMEOUT 600)
