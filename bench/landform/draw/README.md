# bench/landform/draw

**Purpose:** the pictures of `bench_landform` (plan 007 master 3.14): shaded relief with contours, slope classes, plateaus with their escarpments and the ranges, true-scale cross-sections, the equator-to-pole strip with its snowline, and the windows of real blocks.
**Tier:** none; benchmarks are not tiered.
**Owns:** nothing. It draws into the pixels and stream `../index.c` lends, and writes one PNG a call.
**Lifetime:** Process.
**Threads:** main.
**Public functions** (`index.h`): `lf_draw_maps`, `lf_draw_profiles`, `lf_draw_strip`, `lf_draw_windows`.
**May be called by:** `bench/landform/` only.
**Depends on:** `bench/landform/measure/`, `terrain` (`terrain_height_estimate` for the cross-sections), `terrain/climate_sample` (plan 006's fallback) for the snowline, `ui/font` for labels, `display/headless/png`.

Colours are this program's, typed by hand, as `bench_world_map` says of its own; the relief uses that program's ramp, so the two read alike.

**Contours** are drawn where the height's band changes between a pixel and its east or south neighbour: every 50 blocks thin (the shade times 0.65), every 500 thick (times 0.3, the pixel and its south-east one). Plan 007 phase 02 copies these numbers into the Atlas, not this code. None over the sea.

**The snowline** on the strip is plan 006's fallback climate, `terrain_climate_fallback_cell` by each row's y: the height where the warmest month's mean reaches 0 °C, from plan 004's formula at kappa 0.5 and no current. The picture says so.
