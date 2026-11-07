# tests/run/climate/label

**Purpose:** ctest `run_climate_label`. Tests `run/src/climate/label/` (plan 004 phase 10): every day of the year at 45 degrees north and south against a table written here from master plan 4.1's starts and the thirds rule; the phase 10 file's worked examples (day 213 Mid summer, day 349 Late autumn north and Late spring south, day 10 Early winter, day 231 Mid summer and day 232 Late summer); wet and dry between the tropics from the file's harmonics, with 23.5 itself tropical and 23.6 not; polar night and midnight sun only past the circle on a whole day of dark or light; the second labels at 0.26 and not 0.25, and both together, hurricane first; and the text byte for byte, into every buffer size from 0 to 64 with a guard byte that never changes and no middle dot ever cut in half.
**Tier:** test.
**Owns:** nothing.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/climate/label/`.
