# tests/run/failure

**Purpose:** ctest `run_failure`: what happens when the card, a device, the disk or the console goes away from under the running game.
**Tier:** test.
**Owns:** a directory of its own under `/tmp` for each test, removed at its end, and blocks on the card well under 1 GiB.
**Lifetime:** One run.
**Threads:** main, with each test on the card in a child of its own; the real `sim`, `gpu` and guardian in those.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Files:** `index.c` holds the tests; `fixtures/rig.h` what they share, as it came to more than the 1500 lines a file may have; `fixtures/*.script` the two scripts for the built program.
**Depends on:** `tests/harness/`, `tests/run/display/mode/fixtures/`, `tests/run/input/fixtures/`, `tests/run/physics/fixtures/`, everything under `run/`, the built program, a card, and user namespaces.

What is real and what is not:

| Failure | How it is made | Real? |
|---|---|---|
| The card faults | `device_memory_fault_now`, a kernel that writes where nothing is mapped | **Real**: the runtime's own error, sticky, on the real card. Only this process's context goes. The kernel's log gets a line for it each run |
| The card hangs | `game_gpu_pretend_set(..., true)`: the gpu thread stops coming back | The thread is really stuck. The driver is not in it, as no way to hang a driver on a shared card is safe |
| Disk full | A `tmpfs` of 48 MiB, mounted in a user namespace of the child's, filled by a file | **Real**: the kernel's own `ENOSPC`, which a test checks |
| Device unplugged | `tests/run/input/fixtures/`: reads fail with `ENODEV`, as the UAPI says | Fake kernel |
| VT switch | `tests/run/display/mode/fixtures/`, with the signals raised for real | Fake kernel |

No real display, console or input device is opened. The one run of the program has `--headless`.

What it holds to:

- **The card goes:** a real fault under a real game ends the run, with the console back, the world on disk, no block on the books and no leak reported. The program exits 9, says so in the log, and the next run goes on from what was saved. A hang with no error is seen within 10 s and comes to the same, without joining the thread.
- **A device goes:** held keys let go in that frame, a world with a person in it pauses, and the screen says which device until it is back. Plugging it back in ends the words and not the pause. A display that goes ends the run, with the world saved.
- **The disk fills:** what was saved stays, no `.tmp` is left, the HUD and pause screen say so, Quit to Title stays open and offers Leave Unsaved, and everything goes once there is room. Settings and new worlds on a full disk say so too, and leave nothing in the way.
- **VT switch during load:** the switch is answered inside the frame that opens the world, not after it, whether the person comes back during the opening or after it. Nothing goes to the card meanwhile, what was typed on the other VT does not count, and a console not yet taken waits for its VT to show. The signals do not fail a save.
