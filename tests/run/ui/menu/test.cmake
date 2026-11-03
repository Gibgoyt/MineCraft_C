# Included by mc_add_test in cmake/targets.cmake. The test also runs the built program, always with stdin from /dev/null: --headless
# for the whole round, and without it only to see it refused.
set(MC_TEST_ARGS $<TARGET_FILE:minecraft_c>)
