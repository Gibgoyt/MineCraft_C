# 004-ahmed-bohemoth-climate-system — phase 0: The vision, and the plan files

**Status:** done   (updated 2026-11-06)

**Read this file before any other file of plan 004.** Every phase file of this plan assumes you know what is here. An agent who builds the climate without knowing what it's *for* will build something that passes its tests and is useless to the plans after it.

## Goal

Two things are true when this phase is done:

1. Every agent working on plan 004 can say, in a few sentences, what game Ahmed is building, why the climate system exists, what will read it later, and which things it must **not** build yet.
2. The plan files exist: the master plan copied verbatim, and phase files 01 to 11, each with an unticked todo list and the status `not started`, as `plans/README.md` asks.

---

## Part 1: The game Ahmed is building

### In one paragraph

A Minecraft-style voxel game, written from scratch in C (with CUDA for the renderer and NASM for the hot loops), on Linux, for this one machine first. The world is infinite and procedurally generated from a seed. On top of the usual mining and building, it grows a **living world**: settlements of every size, from villages to capitals, grouped into nations, trading with each other by caravan and by sailing ship, with an economy that rises and falls with the land, the seasons and what the player does. It borrows the city-building feel of Stronghold, the questing feel of World of Warcraft, and, from plan 003, some of Hypixel's features. **Nothing about the economy is scripted per map.** It all has to come out of *any* seed, from the land itself.

### The one idea everything rests on

**The land decides; nothing is hand-placed.** Ahmed's words: "Any generated map from any seed somehow needs all of these economics in place", and "given random world generation that drives different strengths and weaknesses between areas which naturally drives an economy".

So the game is built as a stack, worked out strictly top to bottom. Each layer only reads the layers above it:

```
seed
 └─ terrain: height, rivers, seas, caves, ores                     plans 001, 003  (exists, being fixed)
     └─ climate: latitude, sun, seasons, wind, rain, storms          plan 004        (THIS PLAN)
         └─ geology: plateaus, escarpments, massifs, basins           later plan      (not this one)
         └─ vegetation: biomes that follow climate, forests, crops    later plan      (not this one)
             └─ settlements and nations: where people live, and why   later plan
                 └─ economy: jobs, prices, trade, migration           later plan
                     └─ the player: tasks, reputation, building       later plan
```

Geology is drawn beside climate because it changes the ground the climate reads. When it lands, climate is simply recomputed from the new ground: nothing in plan 004 needs to change for that.

Ahmed was firm about the order: "it is 100% strictly top to bottom". If settlements were placed first and then given reasons to exist, the economy would feel random. Placed after the land and climate, a farming village sits where the soil is wet and warm, and a mining village sits by the mountains, because the numbers said so.

### Why the climate system comes first

Every later layer reads it:

| Later layer | What it reads from the climate |
|---|---|
| Vegetation | Temperature and rain normals, and the climate class, to choose biomes and where crops grow |
| Settlements | Where it's pleasant to live: mild temperatures, water, few floods |
| Economy | Seasons: a mountain pass closes when it's below 0 °C, a river route closes when it freezes, a sea lane is cheap along a favourable current. Harvests follow rain. Floods destroy houses |
| Trade | Huge seasonal trade routes "that just make sense", opening and closing with the weather |
| The player | Tasks like "keep the rains from ruining our homes", which come from the flood index |
| Nations | "Seasons ... somehow needs to affect that nation's strengths and weaknesses" |

That's why plan 004 works out things nothing reads yet, like the weekly flood index, soil water and snow. **They are for the economy plan.** Build them properly even though nothing calls them yet.

---

## Part 2: The later plans, so you know what NOT to build now

None of these are part of plan 004. They are written here so that decisions in plan 004 don't block them. If a phase file seems to call for one of these, it's a mistake in the phase file: stop and write it in its **Issues found**.

### Settlements (a later plan)

- **Built physically,** like Stronghold. Villages, towns, cities and palaces are real buildings in the world, and the player can import community builds (castles, palaces) through Ahmed's dev-time toolchain that translates `.schem` and `.nbt` files into the engine's blocks.
- **Tiers by population, not by size of the build:**

| Tier | Population |
|---|---|
| Village | 10 to 99 |
| Town | 100 to 999 |
| City | 1000 and up |
| Capital | A title, one per nation |

- **The player can found a village** by placing beds and a work site, and grow it into a town.
- **The HUD's top corner will show the settlement's name** above the date line that plan 004 adds.

### Nations (a later plan)

- Many settlements fall under one nation, and each nation has a **capital**.
- Nations come from the seed by position, so they work in an infinite world.

### Economy (the plan after climate)

- **Far away, settlements are just numbers.** Population, workers per job, food and goods in store, beds, deposits nearby. Only near the player do they become villagers, carts and ships. This is **level-of-detail simulation**.
- **Jobs and roles come from the land.** A village's role is its biggest job: farming where it's fertile, mining by mountains. Dry land never becomes a farming village, with no rule saying so: the numbers just never make it pay.
- **Prices move with supply and demand,** per nation, with damping so they don't jump.
- **The player can wreck the economy.** An iron farm floods the market, the price crashes, and miners lose their jobs. Industrial mining with TNT can bankrupt an old mining village as miners move to the new one. When the mountain is mined out, they move back, over a year or two.
- **Migration only within a nation,** toward places that are better to live in.

### The universal scorecard (part of the economy plan)

Ahmed's rule: **score outcomes, never causes.** The game never knows what a "drainage system" is. It only measures what it can always measure:

| Vital | Measured as |
|---|---|
| Survival | Deaths against population |
| Food | Food available against food needed |
| Housing | Beds against population |
| Prosperity | Wages against the nation's average |
| Safety | Property destroyed against property standing |

If the player builds drainage and it works, fewer houses flood, fewer people die, and the score rises on its own. A **remembered score** (a moving average) makes people slow to trust a place that just flooded, so a fixed town fills up slowly. **This is why plan 004 must make floods real numbers:** the flood index is what destroys property and kills people in the economy plan.

### Reputation (a later plan)

Separate from the scorecard. Each settlement has its own feeling toward the player, from -100 to +100, like Red Dead's honour. Below a threshold it locks: that settlement is hostile forever.

### Tasks (a later plan)

A villager offers tasks aimed at the settlement's worst vital, worded as outcomes ("we need room for 20 more people"). Some come from the land itself: a bridge over a river, a path through a mountain. They're checked by the vitals at the next census, not by what was built.

### Trade (a later plan)

Shipments always exist as numbers. If a route crosses loaded chunks, a real caravan or a fleet of old sailing ships appears, carrying exactly that cargo. Ahmed has re-implemented ship mods in C for this. If the player destroys it, the cargo is gone, prices change, and reputation falls.

### Ruled out for now

- **No war, no geopolitics.** Ahmed: "Assume that war does not exist in the game we are playing!"
- **Single player only,** for development and testing.
- **No geology in plan 004.** Ahmed: "for now we are only creating a climate system and not yet a geological/rock system please!"
- **No vegetation in plan 004.** Ahmed: "Vegetation systems not yet built into the plan please!" Biomes stay as they are. No autumn colours.
- **No copied assets.** No Hytale files, numbers or art (only ideas), and no Mojang code or data, as plan 001 already says.

---

## Part 3: What plan 004 builds

### Ahmed's decisions for this plan

| Decision | Where it's built |
|---|---|
| 365 days a year. Every 7 days, at 06:00 on the clock, the world pauses and the census screen opens | Phase 01, phase 07 |
| The HUD's top corner shows the date and season (and, later, the settlement) | Phase 01, phase 10 |
| The equator is at y = 0. Positive y is north and positive x is east. The tropic line (23.5°) is 10 km from the equator, so a pole is 38.3 km away | Phase 02 |
| The sun's path follows latitude and season, moving ±23.5° through the year: polar night, midnight sun, mobs spawning by real darkness | Phase 02 |
| Altitude, distance from the sea and large lakes affect climate, worked out once from the seed, not simulated | Phases 03 to 05 |
| Three kinds of ocean current: warm, cold and neutral | Phase 04 |
| Winds | Phase 04 |
| Air cools fast with height: 12 °C per 1000 blocks | Phase 05 |
| Regions blend into each other | Phase 03 onward |
| Hurricanes, cyclones, anticyclones and thunderstorms **spawn**, they aren't simulated. Hurricane seasons and monsoon seasons | Phases 05, 06 |
| A probability outlook that mimics forecasting, without reading the actual future | Phase 07 |
| Weather the player sees: rain, snow, storms, fog | Phase 08 |
| The Atlas on `M`: generated land only, ungenerated land grey, the game keeps running, filters for temperature, rain, wind, ocean currents, pressure systems, rain cells and incoming storms | Phase 09 |

### The phases, and what each leaves behind

| Phase | Name | What's true when it's done |
|---|---|---|
| 00 | Vision and plan files | This file, and every other plan file, exist |
| 01 | Calendar and the weekly pause | The game knows the date, and pauses every 7 days |
| 02 | Latitude and the sun | The sun follows latitude and season |
| 03 | Height estimate and climate cells | Every place knows its height, its distance to the sea, and how continental it is |
| 04 | Currents, pressure and winds | Every place knows its ocean current, air pressure and wind for any day |
| 05 | Temperature, rain and storm seasons | Every place knows its normal weather for any day, and its storm seasons |
| 06 | Weather systems | Storms spawn, move and fade |
| 07 | Daily weather, weekly state, census and outlook | Every week is worked out, saved and reported, with chances for the next |
| 08 | Weather in the world | The player sees and feels it |
| 09 | The Atlas | `M` shows all of it on a map |
| 10 | HUD, warnings and settings | The HUD shows climate and warns of storms |
| 11 | Tuning and cost | Every number is measured and recorded |

### Rules every phase follows

1. **Pure functions of the seed.** Anything that can be worked out from the seed and the time is never saved. Only the weekly state (snow, soil water, runoff, ice, floods) and the calendar are saved.
2. **Deterministic.** The same seed gives the same bytes on 1 worker or 16. Every phase that makes climate output tests that.
3. **Nothing in the terrain changes.** The climate reads the ground through `terrain_height_estimate`, and never changes what generates. No world has to be thrown away for plan 004.
4. **Everything is drawn before it's trusted.** Each phase adds layers to the headless map tool (003 phase 8 stage 1), and Ahmed looks at the pictures.
5. **Every constant has one home,** with a name and a unit.
6. **The repo's own rules:** `plans/README.md`, `plans/CODING_STANDARD.md`, `plans/REPO_STRUCTURE.md`, and `.claude/rules/test-or-build-mutex.md`. Every heavy command goes through `tools/test-or-build.sh`.
7. **Never push.** Ahmed, 2026-11-04: "No please don't push!"

### What plan 004 needs from plan 003 first

| Plan 003 phase | Why |
|---|---|
| Phase 10, coordinates | To check positions and latitude while testing |
| Phase 11, height to +1250 | Climate works out cooling with height over the full world |
| Phase 8 stage 1, the headless map tool | Every phase's pictures are drawn with it |
| Phase 1, worlds survive new blocks | Only phase 08's snow and ice cover. Everything else can go ahead without it |

---

## Todo

- [x] Allocate `004` (already chosen by Ahmed) and check nothing else uses it: `ls plans/004-*` lists only `004-ahmed-bohemoth-climate-system*`
- [x] Copy the approved master plan to `plans/004-ahmed-bohemoth-climate-system.md`, byte for byte, from `~/.claude/plans/<slug>.md`, or check it's already there unchanged: already there, unchanged since Ahmed's commit `01f5505` (see Issues found). Its Status line set to approved on 2026-11-06, nothing else changed
- [x] Create phase files 01 to 11, as `plans/004-ahmed-bohemoth-climate-system-phase-NN-<name>.md`, with every box unticked and the status `not started`: all 11 already exist from Ahmed's planning session, one per phase of master section 10, 0 ticked boxes, all `not started`
- [x] Check every phase file has a Goal, Todo, Verification, Commits and Issues found section, as the README's template asks
- [x] Check every phase file links back to this file for the vision: each of 01 to 11 names `phase-00-vision`
- [x] Report the paths created: none created; the 13 files listed by `ls plans/004-ahmed-bohemoth-climate-system*` were already there

## Verification

No build, test or code changes in this phase.

    ls plans/004-ahmed-bohemoth-climate-system*
    git status --short    # only plans/ changes
    grep -L "## Issues found" plans/004-*phase-*.md    # must print nothing

## Commits

Appended as they land:

- `4efbaae` Mark the climate system master plan approved on 2026-11-06.
- `323a31c` Check the climate plan files and mark phase 0 of plan 004 done.

## Issues found

- 2026-11-06, the verbatim copy. Expected: a byte compare against `~/.claude/plans/<slug>.md`. Actual: no file there mentions climate (`grep -l climate ~/.claude/plans/*.md` prints nothing), so no compare is possible. Evidence it is unchanged: `git log -- plans/004-ahmed-bohemoth-climate-system.md` shows only `01f5505` (Ahmed, "climate system starting point") before this phase. Done: accepted the committed file as the agreed text; only its Status line was changed.
- 2026-11-06, the phase files already existed (written by Ahmed's planning session), so this phase checked them instead of creating them. None was rewritten.
- 2026-11-06, master section 10's own Phase 00 checkbox stays unticked, because the master is only edited on its Status line.
