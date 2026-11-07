# run/src/ui/atlas/layer

**Purpose:** the Atlas's layer registry (`ui_atlas_layers`, 26 rows: key, label, unit, band and overlay functions, scale or
list legend, time mode, number-key group, whether it draws outlines over ungenerated land) and every layer's drawing, plus
one picture's whole draw (`ui_atlas_draw_picture`: bands, then the overlay, then the finish). A later plan adds a layer by
adding a row and its functions.
**Tier:** Borrow (depth 3): the caller lends the target and a scratch arena; band jobs use their worker's job arena.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any. A picture is cut into bands of rows, each a job; every pixel depends only on the view and the world, so
any cut gives the same bytes. Lines, arrows, marks and names cross bands, so the overlay draws them after, in one job.

**Geometry:** blocks a pixel `s = 2^(zoom / 2)`; the tile level `clamp(floor(log2 s), 0, 7)`. The view's pixels are snapped
to world pixels: picture column `i` is world pixel `column0 + i`, `column0 = floor(cx / s) - W / 2`, and its centre is
`(column0 + i + 0.5) s`; rows the same with y flipped. So a pan by any amount samples the same points where two pictures
overlap.

**Bands:**

- `terrain` (and `terrain_lines`, `systems`, `thunderstorm_cells`, `latitude_lines` under their overlays): each pixel the
  ground's colour times hill shading `b` (`draw/`), from the level pixel under its centre; a contour line every 100 blocks of
  height where the height crosses a multiple of 100 between a pixel and the one east or south.
- Fields `climate/layer` draws (phase 03 to 05): `climate_layer_draw_scaled` for the band's rows plus one above, so the
  pressure layer's lines at a band's top see the row north of them, on the world-anchored sample grid; times `b`. Where a
  climate tile is not built yet, the ground shows.
- The Atlas's own values (temperature now, rain radar, last week's rain, wind now, pressure now, snow, soil water, flood):
  worked out on the world-anchored grid (every 4 pixels; temperature now, which reads `climate_weather_at`, every 32) and
  blended between; coloured by the earlier phase's ramp where one drew it (radar, wind, pressure), else by this phase's
  scales; land-only values show the plain ground over sea and where they are 0. The rain radar over grey land is its 1 mm
  an hour edge only.
- Over ungenerated land every band writes `UI_ATLAS_COLOUR_UNGENERATED` and sets the grey mask.

**Overlays:** the climate's own marks (current and wind arrows, latitude lines) through `climate_layer_draw_scaled`'s marks
part; wind now's arrows; pressure now's isobars every 4 hPa by marching squares on world pixels every 4, with H and L;
the systems (hurricanes with ring, eye, spiral, dotted track behind, name and forecast cone; lows with ring, L and a 2-pixel
cold front with triangles toward where it moves; highs with a faint ring and H); thunderstorm cells at 16 blocks a pixel or
closer. Every now layer draws the big systems' rings and hurricanes' names over grey. **The finish** puts the grey back over
ungenerated land on every layer that draws nothing there, and sets every pixel's alpha.

**Public functions:** `ui_atlas_layers`, `ui_atlas_layer_find`, `ui_atlas_layer_next_in_group`,
`ui_atlas_blocks_per_pixel`, `ui_atlas_level_of`, `ui_atlas_geometry`, `ui_atlas_pixel_x`, `ui_atlas_pixel_y`,
`ui_atlas_systems_build`, `ui_atlas_draw_picture`, `ui_atlas_draw_band`, `ui_atlas_draw_finish`, `ui_atlas_estimate_read`,
`ui_atlas_estimate_colour` (a ground from the terrain's height estimate, for the map tool and the golden pictures).

**May be called by:** `run/src/ui/atlas/`, `bench/climate` (the map tool), `bench/atlas`, tests.
**Depends on:** `ui/atlas/draw`, `ui/atlas/tile`, `ui/font`, `ui/compose`, `climate` and `climate/layer`,
`climate/systems` (+ `field`, `name`), `climate/outlook` (`climate_outlook_sigma`, `climate_outlook_error`),
`climate/weather`, `terrain` (the estimate), `job`, `arena`, libm.
