# run/src/climate/systems/field

**Purpose:** pressure, wind and rain of a day list's weather systems at any point and moment (master plan 7.3):

- **Pressure:** each alive system's bell `A(t) e^{-r²/R²}`, nothing past 3 R; cells the same with their own life curve.
- **Wind:** 7.3's profile (`v_max r / r_m` inside `r_m`, `v_max (r_m/r)^0.6` outside), tapered smoothly to 0 from 2 R to 3 R, circling with `σ = s_h e` at the centre, negated for highs: anticlockwise round lows in the north, clockwise in the south, the reverse round highs, and reversed again in strips mirrored past a pole. Cells add no wind here.
- **Rain**, mm an hour: hurricanes `40 |A|/90` at the eyewall, `(r/r_m)²` of it in the eye, `(r_m/r)^0.5` outside times three spiral arms `0.5 + 0.5 cos(3θ - 2.5 σ ln(r/r_m) + 2π n)`, tapered from 2 R; lows a shield `I_s g e^{-d²/(0.6R)²}` centred 0.5 R ahead and showers of `3 I_s / 2.5` mm an hour within about 400 blocks of the cold front; cells `I g (1 - r²/R²)`. All of it raw: what reaches the ground is times the place's wetness, `climate_systems_wetness`, the day's normal over 5 mm (the phase file's Issues found 11).
- **The cold front:** from a low's centre, `front_length` along equatorward turned 30° west, `(-0.5, -0.866 e s_h)`; "on the front" is within 400 blocks of it.
- **A high's cut:** 0.2 inside any alive high's R, else 1. **`f_sys`:** 0.3 in the tropics to 0.5 by smoothstep from 25° to 50°, and down to 0.4 from 60° to 70° (tuned, Issues found 12); `climate_systems_rain_share_today` is that on days some system can rain there (cells when the normal day is over 18 °C, lows within 5° of their band), else 0.

Sums add in list order, so the same list gives the same bytes.

**Tier:** Borrow by depth: allocates nothing.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any; read-only.
**Public functions:** `climate_systems_pressure_at`, `climate_systems_wind_at`, `climate_systems_rain_at`, `climate_systems_rain_factor`, `climate_systems_on_front`, `climate_systems_under_high`, `climate_systems_rain_share`, `climate_systems_rain_share_today`, `climate_systems_wetness`, and the parts `climate_systems_spin`, `climate_systems_wind_speed`, `climate_systems_front`, `climate_systems_thunder_at`.
**May be called by:** its parent `run/src/climate/systems/` (thinning cells), `run/src/climate/layer/`, phases 07 to 09, the bench and the tests.
**Depends on:** its parent (`climate_system_state_at`, the life curve, the constants), the Owner's latitude, `climate/air` (mirror sign), `types`, libm.
