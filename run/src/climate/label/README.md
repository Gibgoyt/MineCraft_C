# run/src/climate/label

**Purpose:** which season it is at a place on a day, as master plan 4.1 labels it, for the HUD's date line (plan 004 phase 10). The caller gives plain numbers (latitude, day length, the cell's rain today and its yearly mean, `s_hur`, whether the monsoon blows today, the day of the year), so nothing here reads a cache.
**Tier:** Arena by depth (2). It allocates nothing.
**Owns:** nothing.
**Lifetime:** Process. No state.
**Threads:** any. Every function is pure.
**Public functions:**

- `climate_season_of`: the label. Past the polar circle (`|phi| >= 90 - CLIMATE_TILT_DEG`, 66.5) on a day of exactly 0 hours, Polar night, of exactly 24, Midnight sun; between the tropics (`|phi| <= CLIMATE_TILT_DEG`, 23.5 itself included) Wet season while the cell's rain today is above its yearly mean, else Dry season; anywhere else the hemisphere's season split into equal thirds, which is `calendar_season_temperate` (phase 01: spring from day 79, summer 170, autumn 262, winter 353, the south half a year apart). Second labels: Hurricane season while `s_hur > CLIMATE_HURRICANE_SEASON_S` (0.25, phase 05), Monsoon while the caller says it blows today.
- `climate_season_text`: `"Mid summer · Hurricane season · Monsoon"`, the parts joined by a space, a middle dot (U+00B7, bytes C2 B7) and a space, the hurricane before the monsoon. Never writes past the buffer, always ends with a NUL when it has a byte, and never cuts a middle dot in half. The hemisphere names are phase 01's (`calendar_season_name`).

Worked examples (phase 10 file): day 213 (shown 214) in the north is Mid summer, as 43 days into a summer of 92 is the middle third; day 349 is Late autumn in the north and Late spring in the south; day 10 is Early winter; late summer starts on day 232.

**May be called by:** `game` (the sim, which works out the input at the body's place), tests, later climate parts.
**Depends on:** `run/src/climate/` (`CLIMATE_TILT_DEG`), `run/src/climate/season/` (`CLIMATE_HURRICANE_SEASON_S`), `run/src/calendar/`, `types`, glibc's libm.
