# bench/landform/draw

**Purpose:** the pictures of `bench_landform` (plan 007 master 3.14): shaded relief with contours, slope classes, plateaus with their escarpments and the ranges, true-scale cross-sections, the equator-to-pole strip with its snowline, and the windows of real blocks.
**Tier:** none; benchmarks are not tiered.
**Owns:** nothing. It draws into the pixels and stream `../index.c` lends, and writes one PNG a call.
**Lifetime:** Process.
**Threads:** main.
**Public functions** (`index.h`): `lf_draw_maps`, `lf_draw_profiles`, `lf_draw_strip`, `lf_draw_windows`.
**May be called by:** `bench/landform/` only.
**Depends on:** `bench/landform/measure/`, `terrain` (`terrain_height_estimate` for the cross-sections), `climate/heat` and `climate/normals` for the snowline, `ui/font` for labels, `display/headless/png`.

Colours are this program's, typed by hand, as `bench_world_map` says of its own; the relief uses that program's ramp, so the two read alike.

**Contours** are drawn where the height's band changes between a pixel and its east or south neighbour: every 50 blocks thin (the shade times 0.65), every 500 thick (times 0.3, the pixel and its south-east one). Plan 007 phase 02 copies these numbers into the Atlas, not this code. None over the sea.

**The snowline** on the strip: no `terrain_climate_fallback` (plan 006) exists yet, so it is plan 004's temperature formula, `climate_heat_from_place`, at kappa 0.5 and no current: the height where the warmest day's mean is 0 °C, that is the warmest day's sea-level mean over `CLIMATE_LAPSE_C_PER_BLOCK`. The picture says so.
