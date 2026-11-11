# tests/run/item/crafting/station

**Purpose:** ctest `run_item_crafting_station`. Tests where each recipe in `config/recipes/` is made, through `run/src/item/crafting/`. It has its own directory because `tests/run/item/crafting/index.c` is at the 1500 lines where a file splits.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/item/`, the generated `tables/`.

No card, no display and no world.

What it checks, since plan 005 phase 6 gave each kind of making its own station:

- The crafting table makes only the other stations: the woodworking and stoneworking benches, the smith table, the tanning rack and the leather workbench. Four logs in the hands make the table.
- Every recipe is made at its own station and laid the same way at no other, and what any grid makes is made at one station only, so nothing is made in two places. Each smelt is done by one process.
- **The walk:** every recipe and every smelt is reached from what the world gives (everything no recipe makes: logs, cobblestone, ores, hides and the rest), what only a block with a tier drops coming once a tool that harvests it has been made. The hands are there from the start, a station once its item is made, a smelt once its block is had and, if it burns, fuel. Nothing circular can be reached this way. It prints the round each station came in, and holds them to Ahmed's order: hands, table, woodworking, then stoneworking and the furnace, the furnace before the smith table, and the tanning rack and leather workbench after woodworking. Since plan 005 phase 11 it walks the alloys too (an alloy once its station is there, its inputs are had and, if it needs heat, its process), and holds the pickaxes to phase 6's proposal: copper and gold before iron, since the smith table is made from copper, and stainless steel after iron. The smith table's recipe takes copper ingots.
- **No armour without the leather workbench:** the same walk with its block never had reaches no armour, and with it all twenty pieces, leather, copper, iron, gold and stainless steel. No piece but diamond's is had except by its recipe.
- Metal armour is leather armour with that metal around it at the smith table; the ingots alone make nothing anywhere.
- Logs to planks, planks to sticks and two logs to sixteen sticks, at the woodworking bench and nowhere else.
- The way a player goes: four logs to a table in the hands, three logs to a bench on the table, then planks, and a pickaxe that the same cells do not make on the table.
- Each station's item places its first part, and every part drops the one item.
- A tanning rack tans a hide to leather in 600 ticks with no fuel and takes none, a furnace will not tan, and iron smelts only in the blast furnace, copper and gold in the furnace.

Placing and breaking a bench are in `tests/run/player/interaction/bench/`, a blast in `tests/run/mob/creeper/`, and its screen in `tests/run/ui/screens/`.
