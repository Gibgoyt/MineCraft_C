# tests/run/item/inventory

**Purpose:** ctest `run_item_inventory`. Tests `run/src/item/` with `stack` and `inventory`, and what `run/src/game/` does with them.
**Tier:** test.
**Owns:** the test arena, and an `item_t`'s region, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main, and one more that turns the wheel; the built program runs its own.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/item/`, the generated `tables/`, the built program, and a card for that program.

Compute only. No real display, console or input device is opened: the program always runs `--headless`.

What it checks:

- **Stacks:** the limit of every item; what is a stack and what is not; moving what fits; a different limit, wear, taking, both clicks in every case; and 200000 steps at random, where every stack stays valid and each item's total adds up.
- **Where things go:** an empty slot in hand does not draw anything; the stack in hand, then others, then the first empty; the hotbar first and then the rows; more than fits; what is no item; a worn tool; every item a block drops; taking away from the last slot back; 200000 steps at random, where `inventory_room` is what `inventory_add` finds and the books balance.
- **The hotbar:** by number and by scrolling, round the ends, at the extremes of a scroll; taking what is in hand; swapping; a shift-click both ways, on to a stack, and against a full hotbar.
- **The Owner:** the region given back, a new world empty and forgetting what was asked, a request taken once, no heap in play, and a wheel on another thread losing nothing.
- **The program:** a script chooses the third slot, mines grass and picks up dirt, which goes to the first slot, so the right button places nothing; choosing the first slot places it, and the player stands where it started.
