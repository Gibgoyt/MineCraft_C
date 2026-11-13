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
- **Plan 008 phase 05's controller** (`game_pace_rung`): the ladder is the render scale then each rung under it down to the floor (75 gives 75, 70, 50); the cost model predicts 19 ms at 100 % and 14.0 at 85 % for P 1 ms and G 18 ms; a late picture while turning steps down one by the next picture, and one more late picture drawn at the rung just left does not step again; a 720 degree a second turn predicted over 13.3 ms steps down before any picture is late, and one at 179 does not; from 50 %, 15 still pictures give 100 % on the 15th and not before where nothing fits, and where the cost allows it steps up one rung after 8 pictures; late pictures with a still camera never step down; a floor of 70 is never passed; off is always the top (the render scale). And a shake of +-60 degrees at 720 degrees a second at 600 MHz, rebuilt from phase 01's cost rose at render distance 64 (9 to 24 ms at 100 %; phase 01 has no per-picture log at that clock), replayed through the controller and the cost model: its predicted intervals' p99 is under 16.7 ms.
- **Uncapped starts now,** with no take known the rule is today's, and a picture waiting and on time holds until main takes it, on main's beat even from a stale next take.
