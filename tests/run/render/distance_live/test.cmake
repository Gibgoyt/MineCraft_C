# Included by mc_add_test in cmake/targets.cmake. The game's own threads against the clock, on the real card, with no display. It times
# itself, so nothing runs beside it. On a slow far field each step waits its 20 s, so about 2 minutes; settled, well under one.
set(MC_TEST_TIMEOUT 900)
set(MC_TEST_RUN_SERIAL TRUE)
