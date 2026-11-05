# tests/run/item/equipment

**Purpose:** ctest `run_item_equipment`. Tests `run/src/item/equipment/`, and what `run/src/game/` does with it.
**Tier:** test.
**Owns:** the test arena, and an `item_t`'s region, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main; the real game test and the built program run their own.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/item/`, `run/src/game/`, the generated `tables/`, the built program, and a card for those last two.

Compute only. No real display, console or input device is opened: the program always runs `--headless`.

What it checks:

- **The tables:** every tool's and every piece of armor's durability and points against the published numbers, what wears stacking to one, and which slot each piece takes. Copper has no published numbers, so its own are in `copper/`, ctest `run_item_equipment_copper`.
- **Mining:** what the slot in hand counts as, gold's tier, and a wooden pickaxe's and a hand's ticks on stone.
- **Wear:** what a block and a hit cost, worked out by hand; every item that wears lasting exactly its durability and leaving by the books; two uses with one left; only the slot in hand.
- **Shears and a hoe:** the block brought out, and never twice; what tills, what does not and costs nothing, a hoe's last use; the button held, every four ticks.
- **Armor:** the pointer, the hand with the books kept, the press and not the hold, shift-clicks both ways with no room; what a hit leaves by hand, against more points, a full set's total, a quarter of a hit as wear, and a piece breaking only after the hit it stopped.
- **At random:** 200000 steps, where every stack stays valid, every item is accounted for and the books balance. The same steps give the same bytes, with no heap.
- **The Owner:** nothing worn at `item_open`, and forgotten at `item_close`.
- **A real game,** down a shaft on the real card and threads: a hoe tills, a helmet goes on, wood on farmland and on stone, a hand on stone at its own time with nothing to show, shears on leaves, a change of tool starting the block again, and a tool breaking on its last use.
- **With everything else, by script on the real card:** armor stops an outside hit and real lava and wears for each, being alight goes past it, a piece that runs out comes off; dying spills what is worn, and that and a worn tool come back as worn as they were; a chest's worn tool likewise; gold on stone in its own 4 ticks; a press that wakes a body does nothing else.
- **The sums:** toughness by hand, and `run/src/player/survival/`'s armor sum held to this module's, over every points, toughness and hit.
- **The hands:** gold's speed and what it harvests; shears rolling nothing of a table in 200 cuts, where a hand does.
- **The program:** it runs with all of this and reports it.
