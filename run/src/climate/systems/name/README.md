# run/src/climate/systems/name

**Purpose:** hurricane names made from syllables (master plan 7.2), so each world has its own. A system's 32-bit `name_seed` picks: bit 0, two syllables (0) or three (1); bits 1 to 5, 6 to 10 and 11 to 15, three numbers each taken modulo 24 into a table of 24 syllables. The first letter is upper case; a third syllable that would make more than 8 letters is dropped, so "Tropical storm " and the name fit in 24 bytes with the zero. Seed 4161 is "Tavela" and 262 is "Orin", the master plan's two examples. A title is "Hurricane" at 33 blocks a second and over, else "Tropical storm"; a low or high is "Low" or "High" and its name.
**Tier:** Borrow by depth: allocates nothing.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** any; pure.
**Public functions:** `climate_system_name`, `climate_system_title`.
**May be called by:** phases 07 (the census's names), 09 (the Atlas) and 10 (the HUD's warning), the bench and the tests.
**Depends on:** its grandparent's `climate_system_state_at`, `types`, libc's string functions.
