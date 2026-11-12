# Included by mc_add_test in cmake/targets.cmake. The test runs bench_landform --self-test, which needs no card.
set(MC_TEST_ARGS $<TARGET_FILE:mc_bench_landform>)
set(MC_TEST_TIMEOUT 280)
