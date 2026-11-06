# tests/run/climate/normals

**Purpose:** ctest `run_climate_normals`. Tests `run/src/climate/normals/`: every knot of `p_belt`, `t_mean`, `a_sea` and `a_land` is the master plan's (6.4, 6.6), at either sign of latitude, so a mistyped knot fails; between knots the curves are the terrain's spline, not a straight line (`p_belt(37.5) = 1017.6375`, `p_belt(20) = 1014.8642`, `a_sea(15) = 1.9500`, `a_land(45) = 13.7921`, `t_mean(28) = 20.9862`, each to 1e-4); past 90° a curve stays at 90°'s value; and the curves built from an arena are byte for byte the ones filled in place.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main only.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/climate/normals/`. No graphics card.
