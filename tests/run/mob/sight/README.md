# tests/run/mob/sight

**Purpose:** ctest `run_mob_sight`, named by its path. Puts a zombie, a skeleton and a creeper in front of a real player in a real game, through `run/src/game/` and on the card, and looks at the picture: each is there, in its own skin.
**Tier:** test.
**Owns:** the test arena, and what its systems reserve, given back by the end.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/game/` and all it runs on, the card (compute only).

Every other test of these mobs looks at their numbers, or at boxes it made itself. Set `MC_SIGHT_DIR` to a directory that exists and it also writes `empty.png` and `mobs.png` there, for a person to look at.
