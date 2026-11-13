# Included by mc_add_test in cmake/targets.cmake. The equator test builds the climate's tiles over 17 km square and every
# real-climate test builds its own, which in a debug build takes minutes.
set(MC_TEST_TIMEOUT 1800)
