# tests/run/save/state/names

**Purpose:** ctest `run_save_state_names`. Tests `run/src/save/state/translate/`, and what `run/src/save/state/` does with its section of names: a state saved under block, drop, item and mob tables that have since moved, at the start of each table and in the middle, with every stack, drop, block number, held item and body compared; a build that has not got something, which takes that out, says so in the log, and leaves a file it believes; a block being mined that has gone, and a stack past what its item allows today; what a grid makes coming from this build's recipes; a state in the old format refused and left as it was; and numbers no list has.
**Tier:** test.
**Owns:** nothing outside what each test starts and stops, and a directory under `/tmp` for each, removed at its end. Other memory comes from the harness's test arena.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/save/`, and the stores in `run/src/player/`, `run/src/item/`, `run/src/entity/` and `run/src/mob/`.

## How a build with other tables is had, with no other build

As in `tests/run/save/world/names/`: the test picks the tables an older build would have had, puts that build's numbers straight into the live stores, lets the real `save_state_capture` write them, and replaces the section of names in the file with that build's, with each hash made to agree. Everything after that goes through the real `save_open`.
