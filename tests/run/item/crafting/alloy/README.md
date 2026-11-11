# tests/run/item/crafting/alloy

**Purpose:** ctest `run_item_crafting_alloy`. Tests the `alloy` recipes of `config/recipes/` and the smith table's alloy slots in `run/src/item/crafting/` (plan 005 phase 11 part B), and the stainless steel they make. It has its own directory because `tests/run/item/crafting/index.c` is at the 1500 lines where a file splits.
**Tier:** test.
**Owns:** nothing outside each test's stack and the test arena.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/item/`, the generated `tables/`.

No card, no display and no world.

What it checks:

- 9 iron ingots and 1 chromium ingot in the two alloy slots, either way round, make 10 stainless steel ingots, unworn, and taking them uses exactly those. More than a making's worth leaves the rest; a pointer that cannot take all ten takes none.
- A shift-click on the output makes as many as the inputs allow into the inventory (64 iron and 5 chromium: 50), and closing gives what is left back.
- 8 iron and 1 chromium make nothing, either way round, nor does either alone, nor 9 chromium and 1 iron. Every pair of items, each at a full slot, and every item alone: only iron with chromium makes stainless steel. Nothing makes it at any other station, nor in any grid or furnace.
- Stainless steel needs no heat: the choice taken is the smith table with no lit blast furnace by it. `smelting_lit_near` gives a bit for each process burning within `CRAFTING_ALLOY_HEAT_REACH` along every axis, for an alloy whose file says `heat`.
- Each stainless steel tool and armour piece is iron's pattern with stainless steel ingots, the armour round its leather piece, at the smith table and at no other station.
- Stainless steel is a tier above iron: it harvests what iron does, faster, its tools and armour last longer, its tools hit as hard or harder, and its armour gives as many points or more and a point of toughness.

That stainless steel is reached from what the world gives is `tests/run/item/crafting/station/`'s walk. That the slots save and reload is `run_save_state`, and their screen is `run_ui_screens`.
