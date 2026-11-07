# run/src/ui/atlas/draw

**Purpose:** the drawing tools the Atlas's layers share, for what `climate/layer` does not already draw: colour scales
(straight-line mixing between stops, rounded half away from zero), hill shading, marching squares with the saddle rule,
points, Bresenham lines (solid or dashed), midpoint circles, discs (opaque or blended), arrows as phase 04 draws them,
forecast cones (the union of circles: its edge as an outline, its inside filled at 25% off grey), the "over grey, outline
only" test, and legends.
**Tier:** Borrow (depth 3): pointer and length from the caller, nothing allocated.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any.

**Hill shading:** with `s` blocks a pixel and the four neighbours' heights, `dz/dx = (hE - hW) / 2s`,
`dz/dy = (hN - hS) / 2s`, `n = (-k dz/dx, -k dz/dy, 1) / |…|`, the light `L = (-0.5, 0.5, 0.707)` from the north-west 45°
up, and `b = clamp(max(0, n · L) / 0.707, 0.4, 1.3)`, so flat ground is 1. `k` is `UI_ATLAS_SHADE_EXAGGERATION` (1, for
Ahmed to tune by eye).

**Marching squares:** corners top-left, top-right, bottom-right, bottom-left; a corner is above when greater than the
level; each crossed edge's point by straight-line interpolation. In the two saddle cases the mean of the four corners
decides: above the level, the two above corners are joined.

**Modes:** `OUTLINE` lands anywhere (rings, tracks, cone edges, names); `FILL` only off grey (fills, arrows, marks);
`ON_GREY` only over grey (another layer's rings there). The grey mask is the picture's.

**Public functions:** `ui_atlas_scale_colour`, `ui_atlas_shade`, `ui_atlas_shade_apply`, `ui_atlas_march`,
`ui_atlas_draw_allowed`, `ui_atlas_draw_point`, `ui_atlas_draw_point_blend`, `ui_atlas_draw_line`,
`ui_atlas_draw_circle`, `ui_atlas_draw_disc`, `ui_atlas_draw_arrow`, `ui_atlas_draw_cone`, `ui_atlas_draw_legend`.

**May be called by:** `run/src/ui/atlas/` and its `layer/`, tests.
**Depends on:** `ui/compose`, `ui/font` (legends), libm, libc (`snprintf` for a legend's numbers).
