# tests/run/save/state

**Purpose:** ctest `run_save_state`. Tests `run/src/save/state/` and `run/src/save/autosave/`, on a small world with no card: a file's bytes by hand; every part reloaded byte for byte and going on the same; flipped bits and forged numbers refused; a stuck write the sim does not wait for; a disk that refuses; children killed at every step of a save and at random moments, with blocks and state checked to be one save; and what a save takes from the sim. What needs the whole game is in `game/`, its own ctest `run_save_state_game`.
**Tier:** test.
**Owns:** nothing outside what each test starts and stops, and a directory under `/tmp` for each, removed at its end. Other memory comes from the harness's test arena.
**Lifetime:** One run.
**Threads:** main, job system workers, and forked children that really die with `SIGKILL`.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/test_world.h`, `run/src/save/`, `run/src/player/`, `run/src/item/`, `run/src/entity/` and `run/src/mob/`. No card and no built program.

"Going on the same" ticks the stores with no mind and nobody at the controls. A mind's next step depends on what a path search last answered, which is left out of a file on purpose: its random numbers and what it was up to are compared as bytes, and it asks again.
