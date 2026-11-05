# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 9: Minecraft's default textures

**Status:** not started   (updated 2026-11-05)

## Goal

The game uses Minecraft's default textures everywhere. Pixel Perfection stays in the repo, untouched, and is what's used when the default pack isn't on the machine.

**Ahmed's words:**

- In chat, 2026-11-05: "we are using pixel perfect minecraft texture pack which I freiking hate!! let's please use default texture pack!!"
- Then: "Let's please download and install default texture pack and start using it everywhere! but do not delete the pixel perfect stuff! Just sed -i all the yaml files to batch change to default texture pack please!!"

**Decided by Ahmed, 2026-11-05:** download the default pack, rather than install Minecraft or use a lookalike.

## Why it can't go in the repo

- **The repo is public.** `gh repo view Gibgoyt/MineCraft_C` says `PUBLIC`.
- **The image is in the repo, not only the YAML.** Each of the 524 folders under `textures/` holds `index.mctex`, our converted pixels, as well as `index.yaml`, and both are committed (`git ls-files textures`). So if the YAML were changed in place and re-imported, `textures/` would hold Mojang's pixels, and the next push would publish them.
- **Minecraft's default textures belong to Mojang.** Plan 001 chose "No Mojang jars, no decompiled code". Using them on this machine is the goal. Publishing them on GitHub is what this phase avoids.

## Approach: a mirror of `textures/` that git ignores

```
textures/          committed, Pixel Perfection and our recipes, unchanged
textures_local/    git-ignored, the same tree, imported from the default pack
```

1. **Download** one version's client jar from Mojang's servers, as the official launcher does, into `/tmp/mc_c_assets/minecraft_default/`. Pin the version and its SHA-1 from Mojang's own version list, and check it. **Never run anything from it:** only `assets/minecraft/textures/` is read, and it's untrusted data.
2. **Copy `textures/` to `textures_local/`,** then batch-change every `index.yaml` there with `sed -i` (or the importer), so its `source:` names the default pack and the default pack's file. This is Ahmed's "sed -i all the yaml files", done in the ignored copy, so nothing committed changes.
3. **Import** into `textures_local/`, writing its `index.mctex` from the default pack's PNGs.
4. **The build uses `textures_local/` when it's there, and `textures/` otherwise,** and says which in one log line. So a fresh clone, or ctest anywhere else, still builds on Pixel Perfection.

```yaml
# textures_local/blocks/hay_side/index.yaml, after the batch change
source:
  kind: pack
  pack: minecraft_default
  version: "<pinned version>"
  licence: Mojang, local use only, never committed
  file: assets/minecraft/textures/block/hay_block_side.png
```

## What needs care

- **Names differ.** Our names map to Minecraft's: `blocks/grass_top` → `block/grass_block_top.png`, `blocks/hay_side` → `block/hay_block_side.png`, and so on. That needs one name map, `textures/import_minecraft_default.yaml`, which **can** be committed, since it holds names and no art.
- **Grey grass and leaves.** Grass tops, leaves and vines are grey in Minecraft, and coloured by biome when drawn. With no biome tint in our renderer yet, bake one green per kind at import. A biome tint would be a later plan.
- **Animated water and lava** are tall strips of frames. Check that our format takes them.
- **Gaps:** blocks Minecraft has no picture for, such as our bench recipes, keep ours from `textures/`. Mob textures need their size and layout checked against our models.

## Todo

- [ ] Add `textures_local/` to `.gitignore`. Check with `git check-ignore`, and add a build check that fails if anything under it is ever tracked
- [ ] Add a downloader that fetches one pinned version, checks its SHA-1, and unpacks only `assets/minecraft/textures/` to `/tmp/mc_c_assets/minecraft_default/`
- [ ] Add `textures/import_minecraft_default.yaml`: our name for every texture the game uses, against the default pack's file, or "keep ours"
- [ ] Copy `textures/` to `textures_local/`, batch-change its YAML `source:` to the default pack, and import its pixels with `compile/src/texture_pack/`
- [ ] Bake grass, leaf and vine tints at import
- [ ] Make `cmake/generated.cmake` use `textures_local/` when it exists, with one line in the build log saying which pack was used
- [ ] Make a sheet of every texture, checked by eye in the game: blocks, items, mobs, UI
- [ ] Keep `textures/ATTRIBUTION.md` true: nothing from Mojang is in the repo
- [ ] Build and test both ways. Without `textures_local/`, every golden picture still passes. With it, the goldens that depend on art are skipped or kept separately, never committed

## Verification

    git check-ignore textures_local/    # must print the path
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 1500 ctest --preset release --output-on-failure
    git status --short    # must show nothing from textures_local/
    git ls-files textures_local | wc -l    # must print 0
    pgrep -a minecraft_c    # must print nothing

Ahmed: build, play, and confirm it looks like Minecraft everywhere.

## Commits

Appended as they land:

## Issues found

**Never committed, never pushed.** Pixel Perfection stays as the committed default. The default pack lives only in `textures_local/` and `/tmp/` on this machine.

**Mojang's terms:** the client jar is downloaded from Mojang's own servers, as the launcher does, and used only on this machine. Ahmed chose this knowing it's on the edge of the terms. It's still his call if he'd rather install Minecraft properly, which needs no other change here.
