# run/src/game/census

**Purpose:** plan 004 phase 07's census, run from the sim: phase 01's trigger owes weeks (its hook, `game_census_week`), and this works them out on the workers, then the report, then the file. Here and not in `ui/screen_census/` (the master plan's names table) because `ui` may not run jobs or call `game`: the screen only draws the report this fills.

**A run**, one step of the sim at a time, never waiting for a job:

1. **Plan:** the store stops taking marks (they wait, `busy`), its tiles are sorted into batches (32 by 32 tiles, so a batch and its 6-tile margin fit the climate's keep square) and blocks (2 by 2 tiles).
2. For each week owed, oldest first: for each batch, **prepare** its climate tiles and the 6 tiles round them (`climate_prepare_begin`, then `climate_prepare_step` each step; the sim leaves the keep square alone meanwhile), then **blocks**, worked by day so 16 workers share even a few blocks evenly: up to 32 slots, each with one of the store's works. **The sim hands out the blocks:** each step, every free slot gets a slot job (`job_try_submit_at`, retried next step if the queue is full). A slot job takes the batch's next block (an atomic counter, in order), runs `climate_week_block_begin`, and spawns 7 day jobs (`job_spawn`, which any idle worker steals), each running `climate_week_block_day`; the last of the 7 to finish runs `climate_week_block_end`, marks the block done and frees the slot, and the sim's next step hands it the next block. No census job queues the next block from inside a job. A fast week's block is done in the reading, so its slot job goes straight on to the next block in a loop, and frees the slot when none is left. If `job_spawn` finds the queues full, the slot job leaves the days not yet queued to the sim (`GAME_CENSUS_SLOT_DAYS_OWED`), which queues them on its next steps. The batch is over when every block is taken and done and every slot is free, so no slot job is still running when the next batch or week hands the slots out again. Weeks up to `fast_until` run fast (4.2: the oldest past 52). After a full week, one **flow** job (`climate_week_flow_run`).
3. **Outlook:** one job fills the report: the player's cell, every kept cell within render distance's events (frost, heat waves, thunderstorms, storms, hurricanes, floods, first snow, ice on lakes), the hurricanes that crossed them, and the outlook at the census moment (06:00 of day 7 W) for where the player stands.
4. **File:** one job writes `<world>/climate/week.<W>` (`save_climate_write`); with no save there is none, and the report says written at once.
5. **Finish:** marks made meanwhile are applied, and the report says written, or failed and why.

Weeks owed while a run goes on join it. **Same bytes on 1 worker or 16:** every block reads pure functions and its own tiles' state, and its days only its work and last week's state, which only its end writes; flow is one job in a fixed order; no sum is split across workers.

**The report** goes from the sim to main through a triple buffer: the sim writes its own buffer and swaps it into the middle; main swaps the middle for its own when it is newer, so neither ever reads a buffer being written. The hook publishes "not written" before the trigger's latch reaches main, so the screen never shows the last census as done.

**A crash:** the trigger moves `last_census_week` on before the census runs (phase 01). If a world opens with the state file at W and the newest week file before W, `game_census_open` owes those weeks again: the same bytes, as all it reads is pure.

**Wiring:** `game_census_attach` at the end of `game_open` (a world with a climate and no other hook gets a census, in the World arena, and `--climate-jump` moves its clock on there), `game_census_detach` in `game_close`, `game_census_sim` in `game_sim_step` (paused or not, so it runs behind the screen; true while preparing, so the sim skips `climate_schedule`), `game_census_mark` as each column commits. Under `--headless` the session steps the sim once a tick while the census screen is up. The game finds its census as the hook's context (`game_census_of`), so `game_t` keeps its size.

**Tier:** Arena: allocates only from the game's World arena (`GAME_CENSUS_ARENA_BYTES`), once, at attach.
**Owns:** nothing; its memory is the World arena's.
**Lifetime:** World: attach at `game_open`, detach at `game_close`.
**Threads:** the sim for every step, the hook and the marks; jobs on workers, each writing only its block's tiles and work, its slot's count of days left and state, the batch's claim counter, or its own `done`; main only for `game_census_report`.
**Public functions:** `game_census_open`, `game_census_close`, `game_census_week`, `game_census_step`, `game_census_idle`, `game_census_quiet`, `game_census_report`, `game_census_attach`, `game_census_detach`, `game_census_of`, `game_census_sim`, `game_census_mark`.
**May be called by:** its parent `run/src/game/`, `game/session` (the report for the screen), the tests and the bench.
**Depends on:** `game` (the game's fields, as a part does), `climate`, `climate/weather`, `climate/outlook`, `save/climate`, `ui/screen_census` (the report's layout), `calendar`, `job`, `arena`, `time`, `log`, `assert`, `types`.
