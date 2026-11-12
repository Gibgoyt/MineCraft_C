# run/src/climate/heat

**Purpose:** temperature, master plan 6.6 (plan 004 phase 05): the normal daily mean `T = t_mean - Γz + B + A s_h cos(2π(d - 170 - ℓ)/365)` as a pure function, with `Γ = 0.012` °C a block (Ahmed's 12 °C per 1000 blocks), the basin heat `B = 0.025 max(0, -z)` on land only, and phase 04's signed swing `A s_h` and lag `ℓ`; the daily range `(4 + 8κ)(1 - 0.6 r)` with `r = 1 - e^(-P_day/6)`, the chance of a wet day of master plan section 8; the snow share `clamp((1 - T)/3, 0, 1)`; and a tile's `t_mean`, which is the yearly mean **at sea level**, `T̄_lat(|φ|) + C × strength / 255`, so a read adds `-Γz + B(z)` for the exact height it is asked about.
**Tier:** Arena by depth; allocates nothing.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any; pure.
**Public functions:**

- `climate_heat_base` (`t_mean - Γz + B`), `climate_heat_wave` (`A s_h cos(...)`) and `climate_heat_formula`, their sum in that order, so the snow-balance model, which keeps the parts apart, gets the formula's bits.
- `climate_heat_from_place`: the same from latitude, height, κ and the current's °C, for 6.6's worked examples.
- `climate_heat_daily_range`, `climate_heat_snow_share`.
- `climate_heat_t_mean`, `climate_heat_build_tile`: a cell's and a tile's `t_mean`.

Worked examples with the spline knots (the phase 05 file): an equator beach 25.914 to 28.014 °C all year; a 45° N peak 1000 high, 1200 blocks inland, -10.50 °C on day 15 and 8.47 on day 196, warmest on day 204; land 150 below sea level at 28° N, 34.63 °C on day 196, 40.35 in the afternoon.

**May be called by:** its parent `run/src/climate/` (the tile job and `climate_normals_at`), `season/` and the layers through the Owner.
**Depends on:** climate/cell (the cell struct), climate/normals (the knots), climate/air (the swing, the lag and the current's °C), `types`, `math/det` (the wave's cosine since plan 006 phase 02, as the snow balance reads it), libm for the daily range's e^x, which decides no block.
