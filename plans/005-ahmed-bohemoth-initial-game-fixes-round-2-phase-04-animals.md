# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 4: Animals, with males and females

**Status:** not started   (updated 2026-11-06)

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

- [ ] Add a male/female field to `run/src/entity/store/`, saved in `run/src/save/state/`
- [ ] Raise the box limit for models, in `compile/src/game_tables/mobs/` and `run/src/entity/animation/`
- [ ] Add `male:` and `female:` parts, scale and yields to the mob YAML, and document them in `config/mobs/README.md`
- [ ] Add daylight spawning on grass in herds, in `run/src/mob/spawn/`, with a cap of its own beside the hostile one
- [ ] Add passive behaviour: wander, keep near the herd, flee when hit

**Stage 2: items**

- [ ] Add items: `beef`, `pork`, `mutton`, `chicken`, `goat_meat` and cooked versions; `egg`, `milk_bucket`; `cow_hide`, `pig_hide`, `goat_hide`; `white_wool` as block and item
- [ ] Add smelting recipes for cooking each meat, and food values for each

**Stage 3: the six animals**

- [ ] Add `config/mobs/{cattle,sheep,pig,goat,mountain_goat,chicken}/` with models and textures (`textures/mobs/<animal>/`) for both sexes, telling them apart as in the table
- [ ] Add shearing, milking and egg laying: females only, apart from shearing, with regrowth and refill timers saved with the world
- [ ] Change the bed recipe to wool

**Stage 4: proving it**

- [ ] Add tests: half males and half females over many spawns; only females milk or lay; goats give no wool; mountain goats only in mountains; drops per animal; saving and reopening keeps sex, wool and timers
- [ ] Add a scripted headless proof: a herd of each from the side, males and females together, checked by eye; a sheep shorn; a cow milked; a bed made from wool and slept in

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1200 ctest --preset debug -R '^(run_mob_.*|run_entity_.*|run_item_.*|run_player_survival|run_save_state|run_playthrough|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: find each animal, tell males from females, milk a cow, collect eggs, shear a sheep, kill a bull for beef, and make a bed from wool and sleep in it. Climb a mountain for mountain goats.

## Commits

Appended as they land:

## Issues found

**Needs phase 3 first,** which turns off the hostile mobs and is where spawn rules change.

**Open for Ahmed:**

1. **Breeding:** a male and a female making young? Not asked, and not in this plan. It's the natural next step, since you've given them sexes.
2. **Do bulls, rams and billy goats fight back** when hit? Default: no, all flee.
3. **Should the bed be two blocks long,** like Minecraft? Default: no, it stays one.
4. **Hides:** cow, pig and goat are planned. Should a sheep give a sheepskin too, as well as wool? Default: no.
