# tests/run/save/power

**Purpose:** ctest `run_save_power`. What a power cut can do to a world, with no power cut: a `SIGKILL` keeps the page cache, so `run_save_state`'s kills cannot say whether the `fsync`s are the right ones in the right order. This records every `open`, `mkdir`, `rename`, `unlink` and `fsync` of a world being made and saved, by standing in front of libc's in this one program, with each still done for real by its system call. It then writes out, for every moment in that record, what a file system that keeps only what `fsync` promises could leave, and opens each as a world.
**Tier:** test.
**Owns:** nothing outside what its one test starts and stops, and a directory under `/tmp`, removed at its end. Other memory comes from the harness's test arena.
**Lifetime:** One run.
**Threads:** main. A job system is started because the autosave needs one, and nothing runs on it: `save_autosave_finish` writes on the thread calling it.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/test_world.h`, `run/src/save/`, `run/src/file/` and `run/src/ui/menu/catalog/`. No card and no built program.

## The model

- A file holds only what it held at its last `fsync`, whatever its name says, and nothing if it has never had one. Its name is no safer for that.
- A name exists as of its directory's last `fsync`. Of the changes to a directory's names since then, a cut keeps each first so many, and each of those leaving out any one, which covers a file system that writes them in another order. One directory takes every choice while the others keep all or none.

That is worse than ext4 or xfs usually are. It is what POSIX and the kernel's documentation promise, so a pass here does not lean on any one file system's habits.

## What must hold after every cut

- Up to the moment the world was reported made, it may be missing. After that it is there.
- It opens. It is never refused, and nothing in it is past reading.
- Its generation is not older than the last save reported finished, and neither is its tick.
- Its blocks are exactly those of its generation, and its state belongs to that same generation.

## What this cannot say

A disk that lies about having flushed, or a controller's cache, is under all of this. Only pulling the plug on the real machine says anything about those, and that is on Ahmed's list in the phase file.
