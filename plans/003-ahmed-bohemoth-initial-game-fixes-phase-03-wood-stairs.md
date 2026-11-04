# 003-ahmed-bohemoth-initial-game-fixes — phase 3: Stairs from every wood

**Status:** not started   (updated 2026-11-04)

## Goal

Stairs can be crafted from every kind of planks, and come out in that wood's colour.

**Ahmed's words:** "creating stairs with wood on a crafting table is not possible"

## What the code shows

The recipe does exist: `config/recipes/oak_stairs/index.yaml`, pattern `P  ` / `PP ` / `PPP`, gives 4. But:

- Its key is `oak_planks` alone, where most wood recipes take `group_planks`.
- `oak_stairs` is the only wooden stairs block in `config/blocks/`.

So spruce, birch or any other planks make nothing. **This is the likely cause, not a confirmed one.** Nothing tests this recipe, and which wood Ahmed used isn't known. The first todo settles it.

`oak_slab` has the same limit: oak planks only, and the only wooden slab. Slabs aren't part of this request, so they're left alone unless Ahmed wants them added (see Issues).

## Where these get crafted

Ahmed asked for stairs on the crafting table, and also for all woodworking to move to the new bench. So this phase makes them work on the table, and phase 4 moves them to the bench. **After phase 4, wooden stairs are bench-only**, and `cobblestone_stairs` stays on the table.

## Todo

- [ ] Add a test in `tests/run/item/crafting/` that oak planks in the stairs pattern make `oak_stairs`, and run it before changing anything, to confirm or rule out a second cause
- [ ] Add a `<wood>_stairs` block and item for each of `acacia`, `birch`, `cherry`, `dark_oak`, `jungle`, `mangrove`, `spruce`: `config/blocks/<wood>_stairs/index.yaml` and `config/items/<wood>_stairs/index.yaml`, with that wood's planks texture
- [ ] Add a recipe for each: `config/recipes/<wood>_stairs/index.yaml`, same pattern, key `<wood>_planks`
- [ ] Add the new stairs to `config/recipes/fuel_wood/index.yaml`, beside `oak_stairs`
- [ ] Add every `BLOCK_ID_*_STAIRS` needed wherever wooden stairs are listed by id: search `run/src/` for `OAK_STAIRS`
- [ ] Add a test that each wood makes its own stairs, and that mixed planks make nothing
- [ ] Update `textures/ATTRIBUTION.md` only if a texture is added; none should be needed

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_item_crafting|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: make stairs from at least oak and spruce, and place them.

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

## Issues found

**Open for Ahmed: should slabs get the same treatment?** `oak_slab` has the same oak-only limit. Adding seven more slabs is the same kind of work, and I'd do it in this phase if you want it.

**Stairs still face one way.** They rise towards `+y` whichever way you stand (Stage 09.1 issue 1). This phase doesn't change that.
