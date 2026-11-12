# run/src/climate/sun

**Purpose:** where the sun is, seen from one place at one moment: its declination for the day of the year, the sunset hour angle and day length, its direction as a unit vector, its elevation and its compass direction. Plan 004 section 5.2, and the derivation in its phase 02 file.
**Tier:** Arena by depth (2). It allocates nothing.
**Owns:** nothing.
**Lifetime:** Process. No state.
**Threads:** any. Every function is pure. The gpu thread asks it for the picture's sun at the drawing camera's y, the sim for the sun's height at the body's feet (beds and mobs), and main for the F3 line and the HUD's polar labels; each from `game_t.clock_units` and a y it already has, so no thread owns the sun.
**Public functions:**

- `climate_sun_declination_deg`: `delta(d) = 23.5 sin(2 pi (d - 79) / 365)`, for a day of the year with its fraction: 0 at the spring equinox (day 79, shown Day 80), 23.5 at day 170.25, -23.5 at day 352.75.
- `climate_sun_sunset_hour_angle`: `h0 = acos(clamp(-sin(phi) sin(delta) / (cos(phi) cos(delta)), -1, 1))`, from 0 (polar night) to pi (midnight sun). With `cos(phi)` under `CLIMATE_SUN_POLE_COS` (a pole) it is pi, 0 or pi/2 by the signs of phi and delta.
- `climate_sun_day_hours`: `24 h0 / pi`.
- `climate_sun_at`: all of `climate_sun_t` at a latitude, a day of the year and an hour on the clock, with `H = 2 pi (t - 12) / 24` and

      s_x = -cos(delta) sin(H)
      s_y =  sin(delta) cos(phi) - cos(delta) cos(H) sin(phi)
      s_z =  sin(delta) sin(phi) + cos(delta) cos(H) cos(phi)        = sin(elevation)

  The azimuth is `atan2(s_x, s_y)` in degrees, clockwise from north (+y), in [0, 360).
- `climate_sun_of_clock`: the same from the world clock and a y: the day of the year from `calendar_year_position` (continuous, so the sun never jumps at 06:00), the hour from the clock's own time of day, and the latitude from `climate_latitude_deg(y)`.
- `climate_sun_height_of_clock`: only `direction[2]`, made by the same code, so the same bits.

**May be called by:** `main`, `game`, `mob`, `player`, and later climate parts. Not `render`: the game hands it the direction as three floats through `render_frame_sun_set`.
**Depends on:** `run/src/climate/` (latitude), `run/src/calendar/` (`calendar_year_position`), `types`, `math/det` (the declination's sine since plan 006 phase 02: the wind latitude reads it), glibc's libm for the rest, which only the sky reads.

On the equator at an equinox (`phi = 0`, `delta = 0`) `s_z = cos(H)`, which is the quantity the bed and mob rules read before this phase, so there they answer as they always did. `render_day_fill`'s fixed sun is this sun at `phi = 0.35` rad (20.05 degrees north) on an equinox.
