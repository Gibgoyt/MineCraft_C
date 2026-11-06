# tests/run/climate/harmonic

**Purpose:** ctest `run_climate_harmonic`. Tests `run/src/climate/harmonic/`: the 12 fit days `365 k / 12`; a half-year step fits to the phase 04 file's worked example (0.5000, 0.1667, 0.6220); a pure wave `3 + 2 cos θ - 5 sin θ` comes back exactly and evaluates back to all 12 values; a constant has no wave; and the 45° N heat-low cell (κ = 1 - e^-2.3) fits to `p0 = 0`, `p1c = 6.721`, `p1s = 1.714`, with day 197 at -6.936 hPa. Phase 05's two-wave fit: two pure waves `5 + 2 cos θ - sin θ + 3 cos 2θ + 4 sin 2θ` back exactly, the worked example's weekly rain to `P0 = 44.583`, `P1c = 0.994`, `P1s = 0.933`, `P2c = -5.250`, `P2s = 14.289` within 1.8 mm of every value, and the first three numbers the one-wave fit's for 100 random sets.
**Tier:** test.
**Owns:** nothing.
**Lifetime:** One run.
**Threads:** main only.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/climate/harmonic/`, `run/src/climate/normals/` (for the heat-low example's swing). No graphics card.
