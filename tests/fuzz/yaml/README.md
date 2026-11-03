# tests/fuzz/yaml

**Purpose:** ctest `fuzz_yaml` and the program `fuzz_yaml`: fuzzes `compile/src/yaml/`.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** see `tests/fuzz/support/`.
**Public functions:** none; a program.
**May be called by:** ctest, and a person: see `tests/fuzz/README.md`.
**Depends on:** `tests/fuzz/support/`, `compile/src/yaml/`. Seeds are built in, and every `.yaml` under `config/` and `textures/`.

## What it holds the parser to

The text and every buffer are exactly the size they should be, with poison round them, and `yaml_parse` has the capacities its header says can never run out.

- A refused document names a line, has a message with an end, and has no root.
- An accepted one has no more nodes than bytes. Each node's kind is real, every text it borrows lies inside the source or the strings, and it is read byte by byte. Links go forward and inside, and `child_count` is what a walk finds.
- Every typed read and lookup on every node returns, with a message that has an end.
- With half the nodes and half the string space it may refuse, by saying so, and stays inside.

What a tool does with a document is not here: `machine_config` and `game_tables` read files in this repository.
