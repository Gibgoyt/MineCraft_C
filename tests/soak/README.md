# tests/soak

**Purpose:** the program `soak`, for hours by hand and never in ctest: runs the real game with no display, over and over on the same worlds, until its time is up, watching for what only time shows.
**Tier:** test.
**Owns:** a directory under `/tmp`, removed unless something failed or `--keep`.
**Lifetime:** One run.
**Threads:** main, and the children it watches.
**Public functions:** none; a program.
**May be called by:** a person. Every preset builds it, from `cmake/hardening.cmake`, so the same soak can be had of `sanitize`, `tsan` and `release`.
**Depends on:** `tests/harness/`, `game` and all it runs on, `save`, `ui/menu/catalog`, the built `minecraft_c` next to it, the card (compute only), and `nvidia-smi` if it is there, to know what is on the card.

## Running it

    tools/test-or-build.sh timeout 14400 build/sanitize/bin/soak

| Flag | Meaning | Default |
|---|---|---|
| `--seconds <n>` | How long. A round is not started if it might run past this | 14100, so as to end inside the timeout above |
| `--program <path>` | The game | `minecraft_c` next to this |
| `--report <path>` | Also write what it says here | None |
| `--keep` | Keep the directory | Only after a failure |

Exit status 0 is a pass. It stops at the first round with a failure, which says what it was, with the end of what the child said.

## What a round is

Sessions are a twelfth of the time, and at least 60 s.

1. **The threaded game**, as a child of this program: sim, gpu and workers with a body walking, digging and placing in frames as fast as they come, saving every 5 s of world time. It must end with no save failed and no `heap_reserve` in the last four fifths of play, and with every heap and device block released.
2. **The built program**, `--headless --menu`: through the real menus into a world, playing, through a pause, the inventory, the overlay, and walks that bring columns in and out; back to the title, into the world again, and on. It must open twice, reach the title, and end at exit status 0 with no heap live and nothing read back damaged.
3. **The same killed**, every other round, by `SIGKILL` once it has saved, as a power cut would. The next session goes on from what it left.

After each, this program opens what was left with `save_open`, as a new game would, and reads every section of every region: none may be refused or damaged, and a world saved has a state to go with it.

Each child is held to: its exit status, no sanitizer report, no more than 8 more descriptors than a tenth of the way in, no more than 256 MiB more memory at the end than the first of its kind, and no more than 3900 MiB on the card.

It uses no display: `--headless` opens no card, console or input device, and the threaded child has never heard of them.

It takes the card's compute, and sits under `tools/test-or-build.sh`, for the whole of its time.
