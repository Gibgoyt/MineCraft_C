# tests/run/item/equipment/copper

**Purpose:** ctest `run_item_equipment_copper`. Tests copper armour in `config/items/`, through `run/src/item/equipment/`. It has its own directory because `tests/run/item/equipment/index.c` is past the 1500 lines where a file splits.
**Tier:** test.
**Owns:** the test arena's inventory, given back by the end of its test.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/item/`, and the generated `tables/`.

No card, no window and no built program.

What it checks:

- Each piece's durability, points and slot against the numbers in plan 003 phase 6, no toughness, and a stack of one. Those numbers are proposed, and Ahmed may change them in one line of YAML each: this table changes with them.
- Every piece lasts longer than gold and not as long as iron.
- Each piece goes in its own slot of the four and no other, by the pointer; and from the hand, with its wear, and with the books kept when it swaps.
- Each piece's points alone, the sum as they go on, and 10 for a set. A hit of 10 on a set leaves 8, worked out by hand, and wears each piece by 2.
- Each piece lasts exactly its durability: the hit that breaks it is still stopped, and the next goes through whole.

How copper is made is in `tests/run/item/crafting/copper/`. A worn copper piece being saved and coming back is in `tests/run/save/state/`.
