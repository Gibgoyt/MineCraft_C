# compile/src/texture_vector/recipe

**Purpose:** paints a procedural texture from the recipe in its `index.yaml`.
**Tier:** Arena (depth 2). Its scratch memory comes from the `tp_arena_t` it is given, and goes back before it returns.
**Owns:** nothing.
**Lifetime:** the caller holds the pixels.
**Threads:** any single thread. There is no shared state.
**Public functions** (`compile/include/texture_vector/recipe/index.h`):

- `tv_recipe_run`: runs one recipe once per frame into the caller's pixels.

**May be called by:** `compile/src/texture_vector/`, and its test.
**Depends on:** `compile/src/yaml/`, `compile/src/texture_pack/support/`.

## A recipe

```yaml
name: moss_block
group: blocks
size: { width: 16, height: 16, frames: 1 }
source:
  kind: procedural
  about: A soft green block.         # one sentence, which becomes the README
  seed: 4242
  ops:
    - { op: fill, color: "#4d6b27" }
    - { op: noise, amount: 18, cell: 3 }
    - { op: noise, amount: 10 }
```

It reads nothing but its own yaml: never another texture, and so never a pack's pixels. Integers and our own SplitMix64 only, so the same recipe gives the same bytes on any machine. Each operation gets its own stream from the seed, the frame and its position, so a recipe with `frames` above 1 varies frame by frame.

Unknown keys, out-of-range numbers and shapes that do not fit are errors with a line and column. Colours are `"#RRGGBB"` or `"#RRGGBBAA"`, and painting replaces a pixel, alpha included.

`x y w h` are an optional region, inside the canvas, defaulting to all of it.

| Operation | Keys | Does |
|---|---|---|
| `fill` | `color`, region | Paints the region |
| `outline` | `color`, region | Paints the region's edge |
| `line` | `color x0 y0 x1 y1` | A Bresenham line, clipped |
| `disc` | `color cx cy rx ry` | A filled ellipse, clipped |
| `gradient` | `from to axis`, region | From one colour to the other, along `x` or `y` |
| `noise` | `amount`, `cell` or `cell_x cell_y`, `seed`, region | Adds up to `amount` to the RGB of visible pixels, from a lattice with one point per cell, which tiles across the whole canvas. A long `cell_y` gives wood grain, and `cell: 1` gives per-pixel grit |
| `speckle` | `color permille seed visible`, region | Paints each pixel with this chance. A transparent colour punches holes. With `visible: true`, only pixels already visible, so that a skin's unused space stays empty |
| `blobs` | `color count radius_min radius_max jitter seed visible`, region | Rough round patches, which wrap inside the region: ore, moss, stones. `jitter` varies each pixel's brightness. `visible` as for `speckle` |
| `bricks` | `color brick_w brick_h offset`, region | The mortar of a running bond, every other row shifted by `offset`, by default half a brick |
| `tint` | `color`, region | Multiplies RGB |
| `copy` | region, `to_x to_y flip` | Copies the region, may overlap, may `flip` in `x` or `y` |
| `art` | `x y palette rows` | Pixel art: rows of text, a character per pixel, each named in `palette`. A `.` leaves the pixel alone |
