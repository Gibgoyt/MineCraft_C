# MineCraft_C

A Minecraft-style game written from scratch in C23, CUDA and NASM, for one machine: see `config/machine.yaml`. It draws by ray casting on the card with CUDA and shows the picture through KMS, with no display server. All the shading, ray casting, vectorization, YAML, PNG, KMS and input code is in this repository. Only glibc, Linux and the CUDA runtime are used.

## Build and test

Heavy commands go through the mutex, which every checkout of this repository shares:

    cmake --preset release
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 1800 ctest --preset release --output-on-failure

The presets are `debug`, `sanitize`, `tsan`, `release` and `profile`, in `CMakePresets.json`.

## Run

From a terminal on your desktop, in a window of its own (Alt+Tab lets go of the mouse):

    ./build/release/bin/minecraft_c              # full screen
    ./build/release/bin/minecraft_c --windowed   # 1280x720, or add --width and --height

Or from a text console (Ctrl+Alt+F3, log in), through KMS with no desktop at all, with the same command. `DISPLAY` decides which. `--headless` runs with no display, and a flag it does not know prints the others.

## Where things are

- `plans/REPO_STRUCTURE.md`: every directory, and "where is...?".
- `plans/CODING_STANDARD.md`: the rules, most of which the build enforces.
- `plans/README.md` and the rest of `plans/`: what was planned, what landed, and what went wrong.
- `textures/ATTRIBUTION.md`: where the art came from, and what is not known about it. It has to go with the program.
