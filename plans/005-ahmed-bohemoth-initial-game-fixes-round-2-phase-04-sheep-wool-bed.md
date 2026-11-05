# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 4: Sheep, wool and a bed

**Status:** not started   (updated 2026-11-05)

## Goal

Sheep live in grassy biomes. Shears take wool from them, and the wool grows back. Three wool and three planks make a bed. Sleeping in it at night skips to morning.

**Ahmed's words:** "add sheep, and have the ability to create a bed please! that is really really needed in order to sleep through the night"

## How it is today

- **There's no wool at all:** no block, no item, no texture.
- **There's no sheep:** `config/mobs/` has only zombie, skeleton and creeper.
- **A bed exists and sleeping works.** `config/blocks/bed` and `config/items/bed`, with sleeping and waking in `run/src/player/survival/index.c:572`. But its recipe is invented: 3 **wheat** over 3 planks (`config/recipes/bed/index.yaml`), from plan 001, because there was no wool.
- **Shears exist,** and so far only cut leaves.
- **Mobs only spawn in the dark** (`max_light` in each mob's YAML), so a mob that spawns in daylight on grass needs new spawn rules.

## Todo

- [ ] Add `config/blocks/white_wool/` and `config/items/white_wool/`, with a texture from the pack or our recipe. Colours other than white are a later plan
- [ ] Add `config/mobs/sheep/`: health 8, slow, wanders, flees briefly when hit, never attacks. Drops 1 wool when killed
- [ ] Add a body model and texture for the sheep, with a sheared look, in `run/src/entity/animation/` and `textures/mobs/sheep/`
- [ ] Spawn rules for a passive mob: in daylight, on grass, in grassy biomes, in small groups, with a cap of its own. Some on the first day, so the first bed can be had. Change `run/src/mob/spawn/` and `compile/src/game_tables/mobs/` as needed
- [ ] Shears on a sheep give 1 to 3 wool, wear the shears and leave it sheared. Wool grows back over time
- [ ] Change the bed recipe to `WWW` / `PPP`, with `W` as `white_wool`, so wheat no longer works. It's made on the crafting table, or wherever phase 6 decides
- [ ] Check sleeping works from the bed in a real night: night only, skips to morning, sets the respawn point
- [ ] Sheep, sheared or not, and their wool regrowth survive save and reload
- [ ] Add tests: spawning in daylight on grass, shearing, regrowth, the bed recipe, and a scripted night slept through, with screenshots checked by eye

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(run_mob_.*|run_entity_.*|run_item_crafting|run_player_survival|run_save_state|run_playthrough|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: find sheep, shear them, make a bed, and sleep through a night.

## Commits

Appended as they land:

## Issues found

**Mobs need phase 3's change to the spawn rules too.** Build after phase 3, so the new rules aren't written twice.

**The bed stays one block long,** as built in plan 001. Minecraft's is two. Say if you want it two long like the bench.
