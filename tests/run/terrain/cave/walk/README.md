# tests/run/terrain/cave/walk

**Purpose:** ctest `run_terrain_cave_walk`. On the default seed, `bench/cave_walk/` must find and walk a way from where a game starts to 24 blocks under the ground, and another to `z = -150`. Then the built game walks the first by the script the tool wrote, unhurt, and must end within a block of where the tool's body did.
**Tier:** test.
**Owns:** nothing but one temporary directory, removed at the end.
**Lifetime:** One run.
**Threads:** main; the two programs run their own.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `bench/cave_walk/`, `TERRAIN_SEED_DEFAULT`, the built program, and a card for that program.

**What it does not say.** Only the first walk goes through the game. The deep one meets skeletons on the way, which shot the body dead when it was tried, so the tool's body alone, which nothing can hurt, is held to that one. Nothing here says a person would find either way, or that the caves are worth exploring. That is for Ahmed.

**Its checks hang on the default seed's ground.** A change of terrain moves the way in. The script is written new on every run, so only a way that no longer exists, or a mob on the short way in, can fail this.
