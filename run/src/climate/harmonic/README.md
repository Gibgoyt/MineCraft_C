# run/src/climate/harmonic

**Purpose:** a yearly cycle stored as an average plus one yearly wave (master plan 6.9): the least-squares fit of `a0 + a1 cos θ + b1 sin θ`, `θ = 2π d / 365`, to the true values on 12 days spread evenly through the year, `d_k = 365 k / 12`, and evaluating a stored wave on any day. For 12 even angles the normal equations are diagonal (Σ1 = 12, Σcos² = Σsin² = 6, every cross sum 0), so `a0 = Σf / 12`, `a1 = Σf cos θ_k / 6`, `b1 = Σf sin θ_k / 6`. The cosines and sines of `θ_k = k × 30°` are a table of exact values, so no cell calls `cos`. Phase 05 adds the two-wave form for rain, `P0 + P1c cos θ + P1s sin θ + P2c cos 2θ + P2s sin 2θ`: waves 1 and 2 on 12 even angles are orthogonal too, so each is its sum over 6, from a second exact table of `cos 60°k` and `sin 60°k`.
**Tier:** Arena by depth; allocates nothing.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any; pure.
**Public functions:**

- `climate_harmonic_day`: `d_k = 365 k / 12`.
- `climate_harmonic_fit1`: 12 values to `{a0, a1, b1}`, summed in `float64_t` in k order, so the same values give the same bytes on any worker.
- `climate_harmonic_at1`: a stored wave on a day from 0 to below 365, fractions allowed.
- `climate_harmonic_fit2`, `climate_harmonic_at2` (phase 05): the same with two waves, `{P0, P1c, P1s, P2c, P2s}`; `at2` is not clamped, so rain readers clamp at 0.

A pure yearly wave comes back exactly (up to rounding). A step-shaped cycle, such as a wind that switches belts through the year, is smoothed: the cost the master plan chose (the phase 04 file, "The wind is not a pure wave").

**May be called by:** the climate's parts that store yearly cycles (`air/`, and phase 05's), its parent `run/src/climate/` to read them, and `run/src/climate/layer/`.
**Depends on:** `types`, libm.
