# tests/compile/texture_vector

**Purpose:** ctest `compile_texture_vector`: proves that `compile/src/texture_vector/` derives the right vector data, that every recipe operation paints what it says, that bad input is refused, and that the committed `textures/` has a source and vector data for everything the game needs.
**Tier:** none; tests are not tiered.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is the ctest `compile_texture_vector`, on `tests/harness/`, with its arguments from `test.cmake`.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/texture_vector/` (run as a process, and its parts linked directly), `compile/src/texture_pack/` parts `support` and `mctex`, `compile/src/yaml/`, and `mkdir` and `rm` from the system. It needs no server, no display, no network, and no pack.

## What it checks

Expected numbers are worked out by hand in the comments next to them.

- **Conversions.** The sRGB table against the formula through `pow`, its inverse, and luma's weights.
- **Alpha class, mean, height, normal, emissive.** Small images with known answers: the mean is in linear light, a transparent pixel's colour does not count, a Sobel step wraps across the edge and points the right way, height stretches across all frames, a nearly flat texture stays flat, and a stick does not shine with its flame.
- **Palette.** Exact at up to 256 colours, the order of its entries, a median cut over that, independence from pixel order, and an outlier that survives.
- **`.mcvec`.** A round trip, every truncation, every field, every bit of the payload, and an index past the palette.
- **Recipes.** Each of the twelve operations, pixel by pixel where it can be drawn out; noise tiles, keeps to visible pixels and stays in range. All of them together are pinned by a hash, so a change in the generator cannot go unnoticed. About forty bad recipes are refused, with a position.
- **The tool.** A small tree of its own: `recipes` writes exactly what the recipe makes and is idempotent, `check` finds edited pixels, edited recipes, missing files, a damaged pack texture, an unmet need and a material for nothing, and `vectors` agrees with what `analyze` makes and keeps modification times.
- **The committed tree.** `check` passes, `vectors` runs over everything, and a table of textures has the right classes: water blends, leaves are cut out, lava shines, and a torch partly shines. Zombie, skeleton and creeper are our own recipes in the player's 64 by 32 layout.
- **No pack needed.** Every recipe, and nothing else, is copied to a clean tree and repainted. The result is byte for byte what is committed, so no recipe depends on a pack's pixels.
