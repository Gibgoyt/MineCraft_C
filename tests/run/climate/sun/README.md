# tests/run/climate/sun

**Purpose:** ctest `run_climate_sun`. Tests `run/src/climate/sun/`: declination 0 at the equinoxes and 23.5 at the solstices; 18.515 and 5.485 hours at 60 degrees north (adding to 24); 12-hour days at every latitude at the equinox, poles included; 24 hours of sun past 66.5 degrees at midsummer and none at midwinter; the south the north half a year apart; the sun a unit vector whose height is the master plan's formula, for 100,000 random places and moments; every worked example of the phase 02 file (45 degrees north at 09:00, sunrise east and sunset west, noon south or north, 60, 70 and 80 degrees north); `render_day_fill`'s fixed sun as this sun at 0.35 rad north on an equinox, every 0.001 of a day; a new world's 08:24 sun from the clock; and the same bits from 16 threads as from one, for `climate_sun_of_clock` and `climate_sun_height_of_clock`.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main, and 16 short-lived threads it joins for the purity test. Each writes only its own slice, and main checks.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/climate/`, `run/src/climate/sun/`, `run/src/calendar/`, and `render_day_fill` from `run/src/render/frame/`, which needs no card.
