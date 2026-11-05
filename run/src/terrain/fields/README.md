# run/src/terrain/fields

**Purpose:** six numbers for any column of the world, from which height (10.3) and biome (10.6) are chosen: continentalness, erosion, ridges, temperature, humidity and weirdness.
**Tier:** Arena: its one struct comes from an `arena_t *` its parent gives it. Sampling allocates nothing.
**Owns:** nothing. Its struct is in the arena `terrain_init` lends, and its fields are in the bank.
**Lifetime:** World: one set of fields is made for each seed.
**Threads:** `terrain_fields_init` and `terrain_fields_deinit` on the thread running `terrain_init`. Sampling from any thread at once, since nothing changes once it is made and each call works in its own stack frame.
**Public functions:**

- `terrain_fields_init`, `terrain_fields_deinit`: a pair.
- `terrain_fields_area_columns`: how many values an area fills.
- `terrain_fields_sample_area`, `terrain_fields_sample_points`: the bulk sampling. A `nullptr` array skips that field.
- `terrain_fields_at`: one column.

**May be called by:** `terrain` and its parts.
**Depends on:** `terrain/noise` (its sibling, for the fields), `arena`, `types`.

## The fields

All six are from -1 to 1. A climate belongs to a column: it has no height in it, and every field's frequency on `z` is 0.

| Field | Means, from -1 to 1 | Cell, in blocks | Octaves |
|---|---|---|---|
| `continentalness` | Deep ocean to far inland | 3072 | 7, weighted 0.3, 1, 2, 2, 2, 1, 1 |
| `erosion` | Sharp high ground to worn-flat ground | 1536 | 5, weighted 1, 1, 2, 1, 1 |
| `temperature` | Frozen to scorching | 4096 | 4 |
| `humidity` | Arid to soaked | 2048 | 4 |
| `weirdness` | Picks the odd variant of a biome: the sign and size are for the biome table | 512 | 4 |
| `ridges` | Valley floor to ridge top: `1 - |3 * |weirdness| - 2||` | From weirdness | None of its own |

**Why continentalness is weighted.** Halving each octave leaves the largest so far ahead that a coast is one smooth blob. With these weights the octaves of 1536 and 768 blocks count the most, then 3072 and 384, so seas have bays and islands and no one scale rules. A heavier first octave makes bigger seas, but over a 16 km map the sea's share then wanders from seed to seed: at a cell of 4096 and a weight of 1 it came to 28.6 to 42.0% on 3 seeds, and at 3072 it took a weight of 0.3, not 0.5, to hold 8 seeds inside the target of 25 to 30%. Land is joined up, whatever the scale, because the sea is well under half of the world: a line of a smooth field that holds under half of it cannot run on forever, so every sea ends. `run/src/terrain/density/README.md` says where the coast is.

**Broad continentalness** is `continentalness`'s first 3 octaves alone, down to a cell of 768 blocks, spread with the same limit, and asked for as `broad`. It costs nothing more, as the field's own sum passes through it. So it follows the coast with the bays and capes left out, which is what `run/src/terrain/density/` wants for a range, whose height over a fine field would be a wall. It is not one of the six fields: nothing here holds it to the shares below.

So `ridges` and `weirdness` are the same noise, twice. That keeps ridges and the odd biomes in the same places, as they should
be, and `ridges` costs no noise of its own. Ridges has a crease at its top on purpose: that is the line of a ridge. Whoever
wants it smooth interpolates.

## How to read a value

For every field but `ridges`, **a value `v` has about `(v + 1) / 2` of the world below it**. So a table can be written in
shares of the world: `temperature < -0.5` is about the coldest quarter, and `continentalness < -0.2` is about 40% of the
world. `tests/run/terrain/fields/` holds each one to that within 2 points of share, at seven cuts, over eight seeds.

`ridges` is weirdness folded, so it is not spread evenly: the share below `r` is about `(r + 1) / 3`, plus `r / 3` if `r > 0`,
which the test holds to 6 points. The real share runs up to 5 points above that just over 0, because every column where weirdness sits at -1 or 1
has a ridges of exactly 0. The high ground is the commoner: half the world is above about 0.2. Ridges is also the steepest
field, up to about 0.09 between neighbouring columns where weirdness is steepest, so a slope built on it should be sized for that.

Exactly -1 and exactly 1 are where the raw noise ran past its limit: measured at about 3.5% of the world, all told, and the
test holds it under 5%. The spread's slope reaches 0 there, so nothing shows where it happens. Do not hang a whole biome on a
value of exactly -1.

## What a value is

For a column `(x, y)`, with `raw(field, x, y)` as `terrain_noise_sample_points` gives it at height 0:

    x' = x + 96 * raw(warp_x, x, y)
    y' = y + 96 * raw(warp_y, x, y)
    u  = clamp(raw(field, x', y') / limit, -1, 1)
    v  = u * (1.5 - 0.5 * u * u)

The warp is what keeps borders from showing the noise's square lattice, and all the fields share it, so a coast and the
climate edge beside it bend together. A raw warp measures a standard deviation of 0.18, so a column typically moves about 17
blocks, and the warp is 6 of the 30 octaves a column costs.

Why the spread: a sum of octaves piles up around 0, so most of the world would sit in a small part of the range. A cubic does
nearly what a bell curve's own distribution function would, without libm, so the same bits come out on any machine.

Each limit was read off the measured raw values at each share: it is about twice that field's standard deviation.
`raw_noise_measures_what_the_limits_were_chosen_for` prints them, and fails if a shape moves away from its limit.

| Field | Limit | Standard deviation measured |
|---|---|---|
| `continentalness` | 0.259 | 0.128 |
| `erosion` | 0.31 | 0.151 |
| `temperature` | 0.35 | 0.172 |
| `humidity` | 0.35 | 0.173 |
| `weirdness` | 0.35 | 0.170 |

A shape, a limit or the warp changing changes every world, and `tests/run/terrain/fields/` pins them.

## What it costs

30 octaves a column for all six fields, in batches of 128. A caller that needs only some asks for only those, since a `nullptr`
array skips that field's octaves. The warp is always paid. Height sampled every four blocks and interpolated costs a sixteenth.
