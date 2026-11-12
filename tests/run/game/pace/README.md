# tests/run/game/pace

**Purpose:** ctest `run_game_pace`. Tests plan 008 phase 02's start rule, `run/src/game/pace/`, on a fake clock: main's takes at 120 Hz, a gpu thread that asks the rule, sleeps or waits as it says, and draws for a fixed or random time.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main only. No card, no threads, no clock.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/game/pace/`.

What it checks, against the numbers in `plans/008-ahmed-bohemoth-smooth-frames-when-turning-phase-02-pacing-and-presenting-without-waiting.md`:

- **A late picture starts the next at once:** ready at 17.1 ms for a take at 16.67, the next starts now, whatever it costs.
- **15 ms pictures start just in time:** with the next take reachable at 16.67 and now 0.1, the start is 0.67 under Q3 A, and 0.1 under B.
- **17 ms pictures give 17 ms intervals:** 600 pictures, readies 17.0 ms apart on average, new pictures on the screen 16.67 or 25 ms apart with a mean of 17.0; today's rule in the same run gives 25.
- **14 ms pictures are one each two ticks:** 300 pictures over 600 ticks, never two started in one tick, each ready 1 ms before its take. The plan says "one a tick"; no 14 ms picture fits an 8.33 ms tick, so it is one each two.
- **Never two in flight:** 2000 pictures of 5 to 30 ms at random, no start before the last ready; and 20 000 random asks never start before now.
- **T-hat is the 90th percentile of sixteen:** 1 to 16 ms gives 15; the oldest go past sixteen; fewer than sixteen take the same rank of what there is.
- **Uncapped starts now,** with no take known the rule is today's, and a picture waiting and on time holds until main takes it, on main's beat even from a stale next take.
