# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 4: Animals, with males and females

**Status:** stages 1 to 4 built and tested: six animals, male and female, with shearing, milking, eggs, a wool bed, the store's limit for animals and a headless proof with pictures   (updated 2026-11-10)

## Goal

Sheep, pigs, cattle, goats, mountain goats and chickens live in the world, each male or female, and you can tell which at a glance. Females give milk or eggs. Males are for food. Animals give meat, hides, wool and feathers. A bed is made from wool.

**Ahmed's words:**

- "we need sheep, pigs, cows, goats, mountain goats (in the mountains)"
- "animals in this game have genders, so bulls + cows, somehow we have to differentiate between the 2 for each, perhaps with size"
- "male sheep have really big curvy horns I think"
- "the same for goats but goats give no wool they only give their hides!"
- "somehow we have to tell the difference between male and female pigs"
- "chickens are quite easy! the roosters are a heck of a lot bigger"
- "only females can be milked or give eggs please! ... just like in real life, all the males are used for food"
- From the round before: "add sheep, and have the ability to create a bed please! that is really really needed in order to sleep through the night"

## Telling them apart

From real animals. These are proposals for Ahmed to change.

| Animal | Male | Female | Male looks | Female gives | Killed, gives |
|---|---|---|---|---|---|
| Cattle | Bull | Cow | Bigger, heavier, horns | **Milk** (bucket) | Beef, cow hide |
| Sheep | Ram | Ewe | Bigger, **big curled horns** | Wool by shearing (rams too) | Mutton, wool |
| Pig | Boar | Sow | Bigger, darker and bristly, **tusks** | — | Pork, pig hide |
| Goat | Billy | Nanny | Bigger, beard, long horns | **Milk** (bucket) | Goat meat, goat hide, **no wool** |
| Mountain goat | Billy | Nanny | Bigger, longer horns. White and shaggy | **Milk** (bucket) | Goat meat, goat hide, **no wool** |
| Chicken | Rooster | Hen | **Much bigger**, tall red comb, long tail | **Eggs**, laid now and then | Chicken, feathers |

**Every male is larger,** about 15–30% depending on the animal, so size works at a distance, and horns, tusks and combs work up close.

## How it is today

- **No animals.** `config/mobs/` has only zombie, skeleton and creeper, and phase 3 turns those off.
- **Mobs spawn only in the dark** (`max_light` in each YAML), so animals need daylight rules.
- **A model allows 1 to 6 boxes** (`config/mobs/README.md`). That's too few for horns, udders, tusks, combs and beards.
- **No male or female.** `run/src/entity/store/` has no such field.
- **Items:** `leather`, `feather` and `bucket` exist. **No** hides, meat, eggs, milk or wool. Shears exist and only cut leaves.
- **A bed exists and sleeping works** (`run/src/player/survival/index.c:572`). Its recipe uses wheat (`config/recipes/bed/index.yaml`), from plan 001, since there's no wool.

## Design

- **One mob per animal, with male and female inside it.** `config/mobs/cattle/index.yaml` has a `male:` and a `female:` part: name, scale, extra body parts (horns, udder, tusks, comb), what it gives while alive and what it drops. So it reads as one file per animal, which suits how mobs are described today.
- **Every animal is born male or female,** 50/50, kept in the entity store and saved with the world.
- **More boxes per model:** about 16, for horns and the like.
- **Passive behaviour:** wander in a herd, flee for a few seconds when hit, never attack. Bulls, rams and billies **might** fight back later, if Ahmed wants it.
- **Spawning:** daylight, on grass, in small herds with both males and females, and a few on the first day so a bed can be had early. Each animal in its own biomes:

| Animal | Where |
|---|---|
| Cattle, sheep, pigs, chickens | Plains, meadows, forest edges, savanna |
| Goats | Hills and windswept land |
| Mountain goats | **Mountains only**: peaks and slopes, high up |

- **From the living animal:** shears on a sheep of either sex gives 1 to 3 wool, which grows back. A bucket on a cow, nanny goat or mountain goat nanny gives milk, which refills over time. A hen lays an egg now and then. **Males give nothing while alive.**
- **Hides** go to phase 6's tanning rack to become leather.
- **Food:** raw and cooked meat for each animal, eggs, and milk, through the existing hunger system.
- **Bed:** `WWW` / `PPP`, with `W` as `white_wool`, so wheat no longer works.

## Todo

**Stage 1: what animals need**

- [x] Add a male/female field to `run/src/entity/store/`, saved in `run/src/save/state/`
- [x] Raise the box limit for models, in `compile/src/game_tables/mobs/` and `run/src/entity/animation/`
- [x] Add `male:` and `female:` parts, scale and yields to the mob YAML, and document them in `config/mobs/README.md`
- [x] Add daylight spawning on grass in herds, in `run/src/mob/spawn/`, with a cap of its own beside the hostile one
- [x] Add passive behaviour: wander, keep near the herd, flee when hit

**Stage 2: items**

- [x] Add items: `beef`, `pork`, `mutton`, `chicken`, `goat_meat` and cooked versions; `egg`, `milk_bucket`; `cow_hide`, `pig_hide`, `goat_hide`; `white_wool` as block and item
- [x] Add smelting recipes for cooking each meat, and food values for each

**Stage 3: the six animals**

- [x] Add `config/mobs/{cattle,sheep,pig,goat,mountain_goat,chicken}/` with models and textures (`textures/mobs/<animal>/`) for both sexes, telling them apart as in the table
- [x] Add shearing, milking and egg laying: females only, apart from shearing, with regrowth and refill timers saved with the world
- [x] Change the bed recipe to wool

**Stage 4: proving it**

- [x] Add tests: half males and half females over many spawns; only females milk or lay; goats give no wool; mountain goats only in mountains; drops per animal; saving and reopening keeps sex, wool and timers
- [x] Add a scripted headless proof: a herd of each from the side, males and females together, checked by eye; a sheep shorn; a cow milked; a bed made from wool and slept in

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1200 ctest --preset debug -R '^(run_mob_.*|run_entity_.*|run_item_.*|run_player_survival|run_save_state|run_playthrough|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: find each animal, tell males from females, milk a cow, collect eggs, shear a sheep, kill a bull for beef, and make a bed from wool and sleep in it. Climb a mountain for mountain goats.

## Commits

Appended as they land:

- `7ce73170` Give animals what they need: a male or female body saved with the world, up to 16 boxes, male and female parts in mob files, daylight herds under their own cap, and a mind that wanders, keeps to its herd and runs when hit (plan 005 phase 4 stage 1).
- `c326eea1` Stage 2: raw and cooked meats with food values and smelting recipes, egg, milk bucket (drinking leaves the bucket), cow, pig and goat hides, and white wool as block and item, each with a vector texture.
- `e88fd341` Stages 3 and 4 of plan 005 phase 4: cattle, sheep, pigs, goats, mountain goats and chickens, each male or female and told apart, with their own skins; shearing, milking and eggs from females (wool from either sheep) with timers saved with the world; a bed of white wool; at most 256 animals in the store; tests and a headless proof with pictures.

## Issues found

**White wool is `tool: none`, so shears do not break it faster.** A block whose tool is shears gets its own item from shears, and wool's table already always drops `white_wool`, so with `tool: shears` it came out twice (caught by `shears_bring_out_what_they_broke_and_nothing_else_does`). The other way round is `tool: shears` with the always-drop row removed, but then wool broken by anything else gives nothing. Kept `none`.

**Needs phase 3 first,** which turns off the hostile mobs and is where spawn rules change.

**Stage 1, as built (2026-11-10):**

- **Sex** is `entity_store_t.borrowed_sex` (`ENTITY_STORE_SEX_NONE/MALE/FEMALE`, the same numbers as the tables' `MOB_SEX_`), given by `entity_store_spawn_t.sex`. A mob with `male:`/`female:` is either, half and half (`mob_spawn_body`), or told (`mob_spawn_body_as`); anything else is none. A male's box is its `scale` times the mob's.
- **Saving:** state format **version 5**, with two new sections and no other change: `SEXES` (13, one byte a body, with the bodies) and `PASSIVE` (14, the animals' mind and every `mob_passive_body_t`, with the minds). Versions 3 and 4 still open, every body as none. A translation that drops bodies drops their sexes in step (tested in `run_save_state_names`).
- **Box limit:** 16 (`MOB_PARTS_MAX`), a mob's own parts and its sex's together. `run/src/game/` now draws any mob through `entity_animation_body_pose`, so a new mob needs no rig in code.
- **YAML:** `mind: hostile|passive`; `male:`/`female:` with `name`, `scale`, `parts`, `drops` (after the mob's) and `yields` (`item`, `count`, `max`, `using`, `every`); `spawn` gains `min_light`, `on`, `biomes`, `min_height`, `max_height`. All in `config/mobs/README.md`. A passive mob has no `despawn`.
- **Spawning:** passive mobs have their own cap, `MOB_SPAWN_ANIMAL_CAP` 10 per 289 loaded columns, counting only those in loaded columns, and are never taken away. Herds are tried with a chance of 1 in 400 a tick, **every tick on the world's first day** (and as near as 12 blocks), on the ground under the sky, male first, female second, then either. The light is not rolled for them: `min_light` keeps them to the day. The hostile cap now counts hostile mobs only.
- **Mind:** `run/src/mob/passive/`, ticked inside `mob_minds_tick`: strolls by `mob/path`, back towards its herd past 8 blocks, runs 5 s at 1.5 times its speed from the player after any hurt (males too), never strikes, drops its kind's and its sex's drops. `yield_wait[4]` per animal runs down and is saved, ready for stage 3.
- **The test animal** is a stand-in `config/mobs/pig/`: passive, boar (1.2, tusks, a leather drop) and sow, on grass in the field biomes, borrowing the creeper's skin, **weight 0** so it never comes in play and is retired from saves. Stage 3 replaces the file. Adding it moves mob ids (pig is 2); saves translate by name.
- **Also changed:** `mob_t` keeps its size (the animals' counters live in the mob arena) so `app_t` on main's stack does not grow; `render_entities_t`'s padding now follows `MOB_COUNT` (room for 30 mobs); four mob tests' arena bounds add `MOB_PASSIVE_BYTES`; the calendar test's version 3 file also drops the new sections.
- **Tests:** new ctest `run_mob_animal` (tables, 4000 bodies about half male, sizes and boxes, the rules, herds filling their cap of 8 in a 9-chunk world in under 50 ticks on the first day and about 1 tick in 400 after, the two caps apart, fleeing, drops, a stray walking back to its herd, no heap); `compile_game_tables` gains 30 bad animal files and a good one; `run_save_state` round-trips a boar and a sow and opens a version 4 file; `run_save_state_names` checks sexes stay with their bodies through a translation. The verification set (18 tests) passes on the debug build.
- **For the orchestrator:** `plans/REPO_STRUCTURE.md` does not yet list `run/src/mob/passive/` or `tests/run/mob/animal/`; rule 4 keeps me out of it.
- **Open:** animals left behind wait in the store forever (they never despawn), so a long journey could fill the store's 1024. Not a problem at 10 per view, but worth a limit once animals have weight.

**Stages 3 and 4, as built (2026-11-10):**

- **The six animals** are `config/mobs/{cattle,sheep,pig,goat,mountain_goat,chicken}/`, each with a 128 by 64 skin of our own in `textures/mobs/<animal>/` (procedural recipes, no Mojang art; each has a `keep:` line in `textures/import_minecraft_default.yaml` and is listed in `textures/vector.yaml`). `config/mobs/README.md` has the table. Males: bull 1.25 with horns; ram 1.2 with big curled ridged horns (6 boxes); boar 1.25, dark brown and bristly with a back ridge and tusks; billy goat 1.25 with a beard and long swept horns; mountain goats white and shaggy (a fringe box), the billy with longer black horns; rooster **1.5**, rust red with a tall comb and a dark green tail. The rooster is past the plan's 15 to 30%, on Ahmed's "a heck of a lot bigger". A pig's and a chicken's own `parts` are only a tail or legs, and each sex carries its own body, so a boar can be dark and a sow pink on one skin. Weights: sheep 12, pig 10, chicken 10, cattle 8, goat 6, mountain goat 6. Mountain goats: the four peak/slope biomes, `min_height: 300`, on stone, snow, calcite or gravel. Goats: the windswept biomes, grove and meadow.
- **Living yields** (`run/src/mob/passive/`): `mob_passive_yields_to`, `mob_passive_take` (rolls `count`..`max`, sets the wait to `every`), and laying in the tick (a yield with no tool lays at the feet when its wait is out). A newly met hen waits a whole `every` (6000 ticks) before her first egg; shears and bucket yields start ready. Ewe and ram: 1 to 3 white wool to shears; cow, nanny goat and mountain goat nanny: a milk bucket to a bucket; hen: an egg every 6000 ticks; every other male gives nothing alive. Waits are in `yield_wait`, already saved whole in `SAVE_STATE_SECTION_PASSIVE`, so no save format change.
- **The game** (`run/src/game/tick/`, `game_animal_use_tick`): a *press* of the right button on an animal (`player_body_aimed`, the swing's aim) with what it yields to in hand. Shears drop the wool at the sheep and wear by 1; a bucket comes back a milk bucket in the slot (with a stack of buckets, the milk goes to the inventory or the ground). From the press until release the hands are held off (`player_t.animal_use`, a former padding byte), so the milk is not drunk at once. A tool that does not wear (durability 0) is what the yield comes in; one that wears brings it out.
- **The shorn look:** a part field `fleece: true` (`MOB_PART_FLEECE` in the tables) is drawn only while the sex's first yield is ready, so a shorn sheep shows its pale skin box until the wool grows back (`mob_passive_fleece_shown`).
- **Bed:** `WWW`/`PPP` with `W` = `white_wool`; wheat no longer makes one. The bed row is gone from `config/recipes/README.md`'s table of stand-ins.
- **The store's limit (the stage 1 risk):** `MOB_SPAWN_ANIMAL_STORE_MAX` = 256 animals in the store, loaded or not. At 256, a herd that would come forgets the animal left furthest behind (waiting for its column and more than `MOB_SPAWN_ANIMAL_FORGET` = 512 blocks away along x or y, past the furthest render distance), counting it in `despawned_count`; with none that far no herd comes. Below 256, a herd may only fill the room left. The random draws are unchanged until the limit is reached. Chosen over unloading animals into column saves, which would touch the save format and column storage.
- **Mob ids moved** (sorted by name): cattle 1, chicken 2, creeper 3, goat 4, mountain_goat 5, pig 6, sheep 7, skeleton 8, zombie 9; `MOB_COUNT` 10. Saves translate by name.
- **Tests:** `run_mob_animal` updated for the real pig and grown by 9 tests (the six told apart; half male over 2000 of each; only females milk or lay, and it comes again; wool from either sheep, 1 to 3, growing back and hiding the fleece box; goats no wool; mountain goats only high in the mountains; each animal's drops are its meat and hide/wool/feathers only; the store limit in three cases; the bed recipe). `run_save_state` round-trips a shorn ram and a hen part way to an egg (sex, wool state and waits). `run_mob_spawn`'s table test has the new ids, and its night test now counts only hostile mobs, as goats may come on its stone floor by day. `compile_game_tables` refuses a non-boolean `fleece`.
- **The headless proof** is a new ctest, `run_mob_animal_proof` (`tests/run/mob/animal/proof/`): a whole real game on the card, grass laid around the body at noon on day 10, pictures written to `/tmp/mc_animal_proof/` (or `MC_ANIMAL_PROOF_DIR`): a herd of each of the six side on (male, female, male, female), a ewe shorn with shears (2 wool, shears worn 1, second press nothing, the ram behind still woolly), a bucket on a bull (nothing) then a cow (milk, not drunk while held), a bed crafted from 3 wool and 3 planks at a table (wheat refused), put down and slept in at 20:24. **Checked by eye:** every herd shows its males bigger with their horns, tusks, beards or combs; the boars dark, sows pink; the rooster red with a tall comb and green tail beside white hens; the shorn ewe shows her pale skin; the bed stands on the grass and the sleeping picture is dark from the bed.
- **For the orchestrator:** `plans/REPO_STRUCTURE.md` should list `tests/run/mob/animal/proof/` and the six `config/mobs/<animal>/` and `textures/mobs/<animal>/` directories. `config/recipes/README.md` still says pumpkin pie leaves out the egg "until hens lay it": hens lay now, so that recipe could take its egg back (not done here).

**Open for Ahmed:**

1. **Breeding:** a male and a female making young? Not asked, and not in this plan. It's the natural next step, since you've given them sexes.
2. **Do bulls, rams and billy goats fight back** when hit? Default: no, all flee.
3. **Should the bed be two blocks long,** like Minecraft? Default: no, it stays one.
4. **Hides:** cow, pig and goat are planned. Should a sheep give a sheepskin too, as well as wool? Default: no.
