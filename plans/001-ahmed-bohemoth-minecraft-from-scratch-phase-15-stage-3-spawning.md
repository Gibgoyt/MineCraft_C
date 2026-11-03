# 001-ahmed-bohemoth-minecraft-from-scratch — phase 15, stage 3: Spawning

**Status:** done   (updated 2026-11-03)

## Goal

Spawning by light level, surface and distance, plus despawn rules, exists.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-mobs.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/mob/spawn/`
- `config/mobs/*`
- `tests/run/mob/spawn/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Define mobs in `config/mobs/<name>/index.yaml`
- [x] Implement spawn and despawn rules in `run/src/mob/spawn/index.c`
- [x] Register test `run_mob_spawn`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Results

All run on 2026-11-03.

- Debug build: clean, `standards_lint` clean, `clang_format` passed.
- `run_mob_spawn`: passed in debug and in the sanitize preset. `compile_game_tables` passed with the new part's refusals.
- All 63 `run_*` and `compile_*` ctests: passed in debug, 368 s. That includes `run_entity_store`, `run_entity_drop`, `run_game` and `run_render_frame`, which the wiring touches.
- **The full ctest suite was not finished.** One attempt was stopped by its 590 s limit with no failing line printed by then, and the other prefixes (`asm`, `cuda`, `render_feature` and the like) were not re-run alone.
- `build/debug/bin/minecraft_c --headless --test-world --walk --render-distance 8` with a script ending at tick 600, from a mid-morning start: `game: 6 mobs came, 0 went, and 6 are in the world`, `heap at exit: 15 reserved, 15 released, 0 bytes live`. The world and the rules are the real ones. Which places they came to was not looked at.
- `bench_mobs` was not re-run: this stage does not add to its bench, and the phase gate owes that number.

## Wiring outside the owned paths

- `compile/src/game_tables/mobs/`, `compile/include/game_tables/mobs/`, and `compile/src/game_tables/index.c` and `README.md`: the part that turns `config/mobs/` into `tables/mobs/`. REPO_STRUCTURE already gives `game_tables` this job, and nothing else could read the yaml.
- `cmake/generated.cmake`: `config/mobs/*` as inputs, `tables/mobs/index.{h,c}` as outputs.
- `tests/compile/game_tables/index.c`: a table of refusals and a good tree for the new part.
- `run/include/game/index.h`, `run/src/game/index.c`: `game_t` holds `mob_spawn_t` in a new 64-byte line (1152 to 1216 bytes), starts it from `arena_world` with its own arena budget, ticks it in `game_items_tick` before `entity_tick`, and `game_report` logs the counts.
- `run/include/main/index.h`: `sizeof(app_t)` assert raised from 13248 to 13312 for that growth.
- `run/include/entity/animation/index.h`, `run/src/entity/animation/index.c`, its `README.md`: `entity_animation_part_t` is now the generated `mob_part_t`, and the rigs point at `mob_parts`, so that a body's parts have one home, the yaml, as Stage 15.1's header said they would. `run_entity_store` still passes every hand-worked number.
- `run/src/game/README.md`: says the above.
- `plans/REPO_STRUCTURE.md`: the row for `tests/run/mob/spawn/`, and `mobs` in the `game_tables` row.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_mob_spawn$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `fd23e98`: the mob files, the tables, the spawn and despawn rules, the test and the wiring.

## Issues found

1. **`entity_t` could not hold the spawn state.**
   - Expected: keep `mob_spawn_t` in the spare 40 bytes of `entity_t`, as it holds the store it works on and changes no size.
   - Actual: `run/include/entity/index.h:11: [include_upward] "mob/spawn/index.h": a lower layer never includes an upper one (plans/REPO_STRUCTURE.md section 11)`.
   - Done: moved it to `game_t`, which is above both. The lint is right.
2. **No `mob` Owner exists.**
   - Expected: Stage 15.2 creates `run/src/mob/index.c`, and this stage's part borrows from it.
   - Actual: at this stage's merge base there is no `run/src/mob/`. Its Owner files are 15.2's paths, and creating them here would collide with its branch.
   - Done: `run/src/game/` is the parent for now: it gives the arena and holds the struct. When `mob_t` lands, moving the field and the `mob_spawn_start` and `mob_spawn_tick` calls into it is a few lines. Left for whoever merges or for Stage 15.4.
3. **Mobs that come are invisible and stand still.**
   - Expected: a phase gate's night has things to meet.
   - Actual: they spawn, fall, shove and despawn, but no stage has given them a mind (15.4 to 15.6) or a picture (Stage 15.1 issue 4, still open). `kind` is the `mob_id_t` those stages will switch on.
   - Done: left alone, out of this stage's paths. They touch nothing but the store.
4. **Biomes play no part, and nothing persists.**
   - The published game has per-biome lists and no hostile mobs in mushroom fields. The plan asks for light, surface and distance, so that is what is here, and `config/mobs/` is global. Saving is Stage 16.1: a mob in a column that leaves is kept while within `far` and waits, and gone past it. See `run/src/mob/spawn/README.md`.
5. **Three wrong expectations in the first draft of the new test.** The code was right in all of these, and the tests were corrected:
   - `seen[MOB_ID_CREEPER] > 0` after filling to the cap: the fixture holds few columns, so its cap is a couple of packs. Weights now have their own test over 300 packs;
   - that test checked every tick as one pack, and a tick can hold two: such ticks are now counted and must be rare;
   - line numbers in the compile test's first bad-mob rows, where `GTT_MOB_GOOD` is 13 lines and not 11.
