# run/src/climate/normals

**Purpose:** the one home of every knot table of plan 004 (master plan rule 5), each joined into a curve with the terrain's own spline (`terrain_density_spline_build`: Fritsch and Carlson's slopes, so a curve passes through every knot and never above or below its two neighbours). From phase 04: the background pressure `p_belt` by the wind latitude |ψ| (master plan 6.4: 0°: 1008, 15°: 1012, 30°: 1020, 45°: 1013, 60°: 1000, 75°: 1010, 90°: 1020 hPa), the yearly mean temperature `t_mean` by |φ| (6.6: 27, 26, 20, 11, 0, -14, -24 °C at the same latitudes), and half the yearly swing over the sea `a_sea` and over land `a_land` (6.6: 1, 3, 6, 10 and 2, 9, 18, 22 °C at 0°, 30°, 60°, 90°). From phase 05: the rain belts `rain_belt` by |ψ| (6.7: 0°: 2400, 10°: 1800, 20°: 700, 30°: 300, 40°: 800, 50°: 1000, 60°: 800, 75°: 350, 90°: 150 mm a year), nine knots: 426.7 at 25°. Since plan 004 phase 11 the tables themselves sit in the header, as `CLIMATE_PRESSURE_KNOTS`, `CLIMATE_TEMPERATURE_KNOTS`, `CLIMATE_SWING_SEA_KNOTS`, `CLIMATE_SWING_LAND_KNOTS` and `CLIMATE_RAIN_KNOTS` with their latitudes, so `run_climate_constants` pins every knot.
**Tier:** Arena by depth (`run/src/climate/normals/`, depth 2): takes one `climate_normals_t` (1480 bytes) from the arena its parent gives.
**Owns:** nothing.
**Lifetime:** Process: the Owner builds the curves once in `climate_init`, into its normals arena, and they are read-only after.
**Threads:** any, once built.
**Public functions:**

- `climate_normals_build`: the curves, taken from an arena. `climate_normals_fill`: the same into the caller's memory, for tests.
- `climate_normals_p_belt`, `climate_normals_t_mean`, `climate_normals_a_sea`, `climate_normals_a_land`, `climate_normals_rain_belt`: each curve at a latitude of either sign (the tables are by |latitude|); past 90° a curve stays at 90°'s value.

Between knots the curve is the spline's, not a straight line: `p_belt(37.5) = 1017.64`, `a_sea(15) = 1.95`, `a_land(45) = 13.79`, `t_mean(28) = 20.99`, where the master plan's worked examples used straight lines (the phase 04 file, Issues found 3).

**May be called by:** its parent `run/src/climate/` and the parts that work out normals from it (`air/`, and phase 05's).
**Depends on:** `run/src/terrain/density/` (the spline only), `arena`, `types`, libm.
