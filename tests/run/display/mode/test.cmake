# Included by mc_add_test in cmake/targets.cmake. The last tests run the built program, always with stdin from /dev/null and
# a card path that does not exist, to see it refuse.
set(MC_TEST_ARGS $<TARGET_FILE:minecraft_c>)
