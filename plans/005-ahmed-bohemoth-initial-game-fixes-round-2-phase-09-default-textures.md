# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 9: Minecraft's default textures

**Status:** blocked: waiting for Ahmed to choose how the textures are obtained   (updated 2026-11-05)

## Goal

The game looks like Minecraft with its default textures, not Pixel Perfection.

**Ahmed's words (in chat, 2026-11-05):** "we are using pixel perfect minecraft texture pack which I freiking hate!! let's please use default texture pack!!"

## The problem with copying them

**Minecraft's default textures belong to Mojang.** Plan 001 chose "No Mojang jars, no decompiled code", and the repo is on GitHub at `Gibgoyt/MineCraft_C`. Committing them would mean publishing Mojang's art. That's the step to avoid, not using them on Ahmed's own machine.

**So the safe way:** the game reads them from Ahmed's own copy of Minecraft when it builds, into a folder git ignores, and they're never committed or pushed. Pixel Perfection stays in the repo as what's used when there's no copy.

**Checked on this machine, 2026-11-05:** there's no `~/.minecraft`, and no Minecraft jar was found. So a copy has to come from somewhere first (see Issues).

## How the import would work

The importer already does this for Pixel Perfection (`compile/src/texture_pack/`, `textures/import.yaml`), so this is a second pack with its own name map:

- **Names:** Minecraft's files are in `assets/minecraft/textures/block/` and `item/` (`stone.png`, `dirt.png`, `grass_block_top.png`, `oak_log.png` and so on), mapped to ours, such as `blocks/grass_top`.
- **Tinting:** grass tops, leaves and vines are **grey** in Minecraft, and coloured by biome when drawn. We have no biome tint yet. Either bake one green per texture at import, or add biome tint to the renderer. Baking is the quick first step.
- **Animation:** water and lava are tall strips of frames. Our format has frames, so this needs checking.
- **What it doesn't cover:** blocks Minecraft has no picture for keep our own, and mobs need a separate check for size and layout.

## Todo

- [ ] **Ahmed:** say where the copy comes from (see Issues), and confirm it's for his machine only and stays out of git
- [ ] Add `textures_local/`, a git-ignored directory for imported Minecraft textures, to `.gitignore`. Verify with `git check-ignore`, and add a check to the build that refuses to commit if they ever show in `git status`
- [ ] Add a second pack to `compile/src/texture_pack/` that reads a Minecraft client jar or its unpacked `assets/`, with a name map for every texture the game uses
- [ ] Make the build choose the Minecraft pack when `textures_local/` has it, and Pixel Perfection otherwise, with one line in the build log saying which
- [ ] Bake grass, leaf and vine tints at import, with one green per kind for now
- [ ] Check water and lava animation, and every block and item, with a sheet of all textures checked by eye
- [ ] Keep `textures/ATTRIBUTION.md` true: nothing from Mojang is in the repo, and say so
- [ ] Test the build both ways, with and without `textures_local/`. Pixel Perfection's goldens must still pass without it

## Verification

    git check-ignore textures_local/    # must print the path
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 1200 ctest --preset release --output-on-failure
    git status --short    # must show nothing from textures_local/
    pgrep -a minecraft_c    # must print nothing

Ahmed: play and confirm it looks like Minecraft.

## Commits

Appended as they land:

## Issues found

**Open for Ahmed: where does the copy come from?**

1. **Install Minecraft with the official launcher** and the build reads the jar from `~/.minecraft/versions/<version>/<version>.jar`. You need to own Minecraft. **Recommended.**
2. **Download the client jar from Mojang's servers for one version,** which the launcher does too, into `/tmp/`, and import from there. No install, but it's on the edge of Mojang's terms, so it's your call.
3. **Use an open pack made to look like the default,** which can go in the repo. I haven't found one with a clear open licence that looks the same. Faithful, for example, requires permission.

**Never committed, never pushed.** Whichever you choose, Mojang's textures stay on this machine only.
