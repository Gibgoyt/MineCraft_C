# bench/landform/measure

**Purpose:** the measures of `bench_landform`: the jobs that read the profile and real blocks, the sliding squares, and every number of plan 007 section 4's table, worked out from a square grid of heights. Kept apart from `../index.c` so no file passes 1500 lines, and so the self-test and the seeds run the very same code.
**Tier:** none; benchmarks are not tiered.
**Owns:** nothing. Every buffer is lent by `../index.c` in an `lf_memory_t`.
**Lifetime:** Process.
**Threads:** main, and the jobs `lf_jobs` submits: each job takes items from one counter and writes only its own rows, so 1 job and 16 give the same bytes.
**Public functions** (`index.h`): `lf_measure`, `lf_windows`, `lf_jobs`, `lf_class`, `lf_percent`, `lf_gradient`, `lf_place_at`, and the names of the table's rows and places.
**May be called by:** `bench/landform/` and `bench/landform/draw/` only. A directory under `bench/` with an `index.h` is compiled into the program of the directory above it (`cmake/targets.cmake`), not made a program of its own.
**Depends on:** `terrain` (the profile through `terrain_height_estimate_area`, real blocks through the stages `bench_world_map` runs), `job`, and the generated `tables/blocks/`.

## The measures

The definitions are plan 007 phase 01's, "Background, explained". Where a size in blocks is not a whole number of columns, it is cut to one: at 16 blocks a column, a half of 1500 is 93 columns.

- **Gradient:** over 8 blocks, by central differences. On the map the sweep reads the profile 4 blocks either side of each column. On a window, and on the self-test's ground, it is the columns either side, 4 blocks apart. The self-test's ground uses one-sided differences at its border.
- **Relative height:** in a square of 2048 blocks round each column, (h - lowest) / (highest - lowest). The deciles are fixed bins of 0.1, over land where that relief is at least 150. Their medians come from 6002 bins of a thousandth of a decade each, so a median is good to 0.12%.
- **Peaks:** land, strictly above its four neighbours, the highest in a square of 512, at least 150 over the lowest in a square of 3 km. Of peaks that tie within one square, the first found stands. 16 rays from each, every grid step (16 blocks on the map, 4 on the self-test's ground), stopped where no new lowest has come for 64 blocks, or at 3 km, or at the grid's edge. A ray that never falls is left out; a peak with fewer than 8 rays left is counted but not measured. `r50` is found between samples; `r100` is where the ray was lowest. Concavity is (median `r100` - median `r50`) / median `r50`; W50/H is 2 median `r50` over the median drop.
- **Plateau land:** at 250 or over, and inside **some** square of 512 whose relief is at most 80 (a sliding square's relief, then the same square slid again over where that held). The plan's words, "whose local relief within 512 blocks is at most 80", read with the square centred on the column, would cut 256 blocks off every plateau's rim, so a 5 km plateau would read 4.6 km. Read this way, a plateau takes its rim down to 80 below its top. Pieces join by faces.
- **Escarpment:** a plateau column with a face on a column that is not plateau, at least 300 over the lowest in the square of 3 km round it. Each counts as one grid step of length.
- **Range pieces:** land at 500 or over, joined by faces. Length is the longest of the extents along x, y and the two diagonals; elongation is that over the square root of the area.
- **Knobs:** land strictly above its four neighbours, the highest in a square of 128, at least 15 over the lowest in it. A flood that always takes the highest ground it has reached, kept within 512 blocks each way, finds the saddle: the lowest it has taken when it first meets higher ground, or the lowest in reach if there is none. Prominence 15 to 150 is a knob; the saddle is its base.
- **Pieces of less than 0.1 km^2** are not counted, as in `bench_world_map`.
