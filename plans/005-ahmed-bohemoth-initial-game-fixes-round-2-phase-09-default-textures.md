# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 9: Minecraft's default textures

**Status:** done, waiting for Ahmed to play with it   (updated 2026-11-10)

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

- [x] Add `textures_local/` to `.gitignore`. Check with `git check-ignore`, and add a build check that fails if anything under it is ever tracked
- [x] Add a downloader that fetches one pinned version, checks its SHA-1, and unpacks only `assets/minecraft/textures/` to `/tmp/mc_c_assets/minecraft_default/`
- [x] Add `textures/import_minecraft_default.yaml`: our name for every texture the game uses, against the default pack's file, or "keep ours"
- [x] Copy `textures/` to `textures_local/`, batch-change its YAML `source:` to the default pack, and import its pixels with `compile/src/texture_pack/`
- [x] Bake grass, leaf and vine tints at import
- [x] Make `cmake/generated.cmake` use `textures_local/` when it exists, with one line in the build log saying which pack was used
- [x] Make a sheet of every texture, checked by eye in the game: blocks, items, mobs, UI
- [x] Keep `textures/ATTRIBUTION.md` true: nothing from Mojang is in the repo
- [x] Build and test both ways. Without `textures_local/`, every golden picture still passes. With it, the goldens that depend on art are skipped or kept separately, never committed

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

- `e993e1f1` Build with Minecraft's default textures from a git-ignored textures_local/, keeping Pixel Perfection as the committed default.

## Issues found

**Never committed, never pushed.** Pixel Perfection stays as the committed default. The default pack lives only in `textures_local/` and `/tmp/` on this machine.

**Mojang's terms:** the client jar is downloaded from Mojang's own servers, as the launcher does, and used only on this machine. Ahmed chose this knowing it's on the edge of the terms. It's still his call if he'd rather install Minecraft properly, which needs no other change here.

### Done, 2026-11-10

**Pinned: Minecraft 26.3**, the latest release in Mojang's list that day. Client jar SHA-1 `e877b6a07acd633fb3bb475002175cec036e7b87`, 41,483,720 bytes, 3964 PNGs under `assets/minecraft/textures/`. `tools/fetch_minecraft_default.sh` checks the version JSON against the SHA-1 in Mojang's list, then the jar against the pinned SHA-1, unpacks only `assets/minecraft/textures/*` with `unzip` into a new empty directory, refuses anything but files and directories, and only then writes `mc_c_pin.txt` (`26.3 <sha1>`) beside it. The importer requires that file to match the map.

**Not `sed -i`, the importer.** The phase allowed either. The YAML is rewritten by `texture_pack import-default` (new part `compile/src/texture_pack/default_pack/`) in a fresh copy made by `tools/import_minecraft_default.sh`, because the pixels have to be re-imported anyway, and `import` could not be reused: it pins a git commit, wants licence evidence, and requires every file in the pack to be accounted for, none of which fits a jar of 3964 files. Its own checks instead: the pin, every texture in the tree named once by the map (so a new texture is refused until someone maps it), the result the same width and height as ours, and the same frame count or 1 (an animation may become a still; only the crack's frames are drawn, and it must keep all 10). Everything is decoded and checked before the first write. Tests: two new cases in `compile_texture_pack`, on a stand-in pack the test makes, never the real one.

**The map: 402 from Minecraft, 122 of ours kept**, each with its reason in `textures/import_minecraft_default.yaml`. Kept: beds, chests, doors and signs (Minecraft draws them as models with one skin), rail crossings and junctions, brake rails, our bench, desert stone, obsidian bricks and glass, corals (other colours), potions (Minecraft tints one grey bottle), the jungle grass and river flow at 32x32, the minecart and arrow, and most of the UI: font, logo, hotbar (Minecraft's is 182 wide, ours 162), panel, slots, bars, sun and moon. From Minecraft in the UI: heart, empty heart, bubbles, armour, empty hunger, buttons, slider, text field, armour slot icons. Renames such as `grass_top` to `grass_block_top`, `wool_dark_grey` to `gray_wool`, `wheat_stage_1` to `wheat_stage0`, wooden and golden tools, fences from planks. Animated strips are cropped: water and lava keep 16 frames of the still strip (the flow strips are 32 wide), fire 8 frames, kelp, sculk and seagrass their first frame. The player and zombie take the top 64x32 of Minecraft's 64x64 skins, which is the old layout ours copies; skeleton and creeper are already 64x32.

**Tints baked at import** (`tints:` in the map, multiply then round): grass `91bd59` (plains) on grass tops, the grass side overlay (composited over `grass_block_side`), short grass, ferns and sugar cane; foliage `77ab2f` on oak, jungle, acacia, dark oak leaves and vines; birch `80a755`, spruce `619961`, mangrove `8db127`; water `3f76e4`; lily pad `208030`; stems `e0c71c`. Our dry grass uses Minecraft's grass with the savanna colour `bfb755`, and river water is ours, `5a8fea`.

**One threshold changed in the copy only.** `vector.yaml` asks glow lichen for luma 180 before it shines; Minecraft's brightest lichen pixel is 172, and texture_vector refuses a light row no pixel passes. The import script lowers that row to 170 in `textures_local/vector.yaml` and fails loudly if the committed row ever changes.

**Build.** `cmake/generated.cmake` picks the tree at configure and prints `-- Textures: textures_local/, Minecraft's default pack, on this machine only` or `-- Textures: textures/, Pixel Perfection and our recipes`; the blob step names the tree too. A glob with `CONFIGURE_DEPENDS` reconfigures when `textures_local/` appears or goes. `mc_check_textures_local` (`cmake/check_textures_local.cmake`) runs before anything compiles and fails if git tracks anything under it. Tested by force-adding a probe file: the check failed with the file named; after `git rm --cached` it passed.

**Sheet.** A contact sheet of all 524 textures from `textures_local/` was made in the scratchpad (never committed) and looked at: grass, leaves, water, ores, logs, crops, tools, mobs and the HUD icons all read as Minecraft's. Seeing them in the game is Ahmed's check.

**Tests, release.**
- Without `textures_local/`: build clean, 151 tests, every golden picture passes. Two compile tests (`compile_sound_pack`, `compile_texture_vector`) failed once with `mkdir` and write errors while `/home` was at 100% (2.2 GB free, other agents' builds); rerun alone they pass. See the final report.
- With `textures_local/`: build clean; 16 tests compare with the committed art and fail on it, all on golden pictures, picture hashes, or (run_mob_sight) the zombie's skin being green enough: `render_feature_{entities,lod,post,shapes,sky,under_water,water,weather}`, `run_mob_sight`, `run_render_frame`, `run_ui_{census,font,hud,hud_corner,menu,screens}`. They are `MC_ART_TESTS` in `cmake/targets.cmake`, disabled only when the build uses `textures_local/`: ctest then shows 135 of 135 passed and 16 not run. No golden is regenerated from Mojang's art.

**For later.** Under the sea, 30 pixels go fully black with Minecraft's darker water where none do with Pixel Perfection (`render_feature_under_water`); worth a look if Ahmed sees it. Biome tint in the renderer would replace the baked colours. Animated water, lava and fire would want the renderer to step frames, which it does not do for anything but the crack.
