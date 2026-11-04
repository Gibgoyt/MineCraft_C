# Included by mc_add_test in cmake/targets.cmake. The test also runs the built program, always --headless and with stdin from
# /dev/null, and the tool that writes its script.
set(MC_TEST_ARGS $<TARGET_FILE:minecraft_c> $<TARGET_FILE:mc_bench_cave_walk>)
set(MC_TEST_TIMEOUT 280)
