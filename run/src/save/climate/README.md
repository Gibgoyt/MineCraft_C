# run/src/save/climate

**Purpose:** plan 004 phase 07's weekly state on disk, in its own file beside the world's: `<world>/climate/week.<W>`, the store after census week W. Its own file, not the state file, because it changes once a week while the state file is rewritten every 30 seconds, and it can be megabytes (100 km² of explored land is about 200 kept tiles with margins, 3 MiB).

- **Format:** a 64-byte `save_climate_header_t` (magic `MCCLIMWK`, seed, week, the tiles' byte count and FNV-1a hash, version `SAVE_CLIMATE_VERSION` = 1, header size, tile count, padding, and the header's own FNV-1a checksum), then every tile, `climate_week_tile_t` as it is in memory (16,448 bytes), in (tile_y, tile_x) order, every padding byte zero. Little-endian. The same store gives the same bytes.
- **Writing** (`save_climate_write`, from the census's file job): to `week.<W>.tmp`, synced, renamed over `week.<W>`, the directory synced, so a crash leaves the old file or the new one whole. Then every week file but the newest two is removed (`SAVE_CLIMATE_KEEP`): the one before stays until a later census, so a crash between this file and the state file's next save still finds the week the state file names.
- **Opening** (`save_climate_open`, as a world opens): the newest `week.<W>` with W no later than the state file's `last_census_week`. Everything is checked: magic, version, seed, the header's checksum, sizes, the tiles' hash, strict order, flags, every number finite, soil within 0 to 150, snow, ice, runoff, rain and flood at least 0, day kinds below `CLIMATE_DAY_KIND_COUNT`, every padding byte zero, and each tile at week W. A file that fails is refused and left alone, the store is empty, and the census starts from spin-up and the log says so: a forged number with a good hash is still refused, as `run/src/save/state/README.md` does. On success every other name that is exactly `week.<digits>` or that with `.tmp` is removed, and no other name.
- **Which file goes with the state file:** the census for week W moves `last_census_week` on as it begins (phase 01), and writes `week.<W>` when it is done. If the state file says W but only `week.<W−1>` is there (killed in between), the census (`run/src/game/census/`) loads that and runs week W again, which gives the same bytes, as everything it reads is pure. A world from before this phase has no `climate/` directory: every tile starts from spin-up at `last_census_week`, and the log says so once.

`SAVE_WORLD_VERSION` and `SAVE_STATE_VERSION` do not change: `save_world_open` and `save_state_open` only remove names they make, so neither touches `climate/`.

**Tier:** Arena by depth; allocates nothing. Reads straight into the store's tiles, and writes straight from them.
**Owns:** nothing.
**Lifetime:** none: every call is complete in itself.
**Threads:** any thread but one at a time per world; nothing may add to the store while either runs (the census holds marks while it works).
**Public functions:** `save_climate_path`, `save_climate_write`, `save_climate_open`, `save_climate_hash`.
**May be called by:** `game` (the census), the tests.
**Depends on:** `climate/weather` (the store and its tiles), `file`, `log`, `types`, libc's `opendir`.
