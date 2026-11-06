# tests/run/climate/systems

**Purpose:** ctest `run_climate_systems`. Tests `run/src/climate/systems/` and its parts `field/` and `name/` (plan 004 phase 06): the inverse CDF's worked examples and 10^6 real hashes at three rates (mean, variance and share of zeros); the forced 120-day season giving 2.4 hurricanes a square, and the real count printed; the track-time sea temperature at 15° N (27.95 °C, `s_hur` 0.725; 31.95 and 1 with a +4 current, equal to phase 05's cell); 20 years of hurricanes only in season, starting between 5° and 25°, every sample at 5° or more; hurricanes going west and poleward, a day into the turn within 15° of poleward and east two days after, poleward as -y in the south and in a mirrored strip; weakening by half a day over land and over sea under 26 °C, not over warm sea; the spin round lows and highs at 45° N, 45° S and 45° N mirrored, with the worked speeds; the hurricane's worked wind, pressure and title; a low's front toward the equator; names from syllables; a track the same whichever day it is first asked for; 1 and 16 workers the same lists, held to a pinned digest; negative coordinates and a list against one built by hand; no track past a pole and no high at the equator; lists never overflowing; the mask's point read against the page; cells' rates (9 afternoon, 36 on a front, 1.8 under a high), start hours, motion with the wind, and a fifth of the rate within 0.3 R of a high's centre over 1000 days.
**Tier:** test.
**Owns:** nothing; memory from the test arena.
**Lifetime:** One run.
**Threads:** main, and the job workers that build tiles and, in one test, lists.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/climate/` and its parts, `terrain`, `job`, `thread`, `time`, `tests/run/climate/fixtures/`. No graphics card.
