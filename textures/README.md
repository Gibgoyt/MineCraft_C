# textures

Translated texture data, committed, so that a build needs no pack and no network. Licences and credits are in `ATTRIBUTION.md`, which is generated, but for one section written by hand, and ships with anything that ships these textures.

```
textures/
├── ATTRIBUTION.md             every source pack, author, licence and commit
├── import.yaml                what the import takes from the pack, and why it leaves out the rest
├── vector.yaml                what pixels cannot say (light, blending), and every texture the game needs
├── blocks/dirt/
│   ├── index.mctex            the pixels: see compile/src/texture_pack/mctex/README.md
│   ├── index.yaml             where it came from and its size, or its whole recipe
│   └── README.md              its credit, and what was changed
├── items/
├── mobs/
└── ui/
```

Groups: `blocks`, `items`, `mobs`, `ui`. Every directory below a group is one texture, and has an `index.yaml` and a `README.md`.

`source.kind` in `index.yaml` says where a texture comes from, and there are two: `pack` means imported, and checked against `import.yaml`. `procedural` means painted by the recipe in that file, from nothing else, and it owes no credit. **No Mojang textures and no decompiled code**, ever. What this repository cannot prove of the pack is in `ATTRIBUTION.md`.

## Without the pack

The game and its build read only this directory. Nothing in the build refers to the pack, so it may be missing, and a network is never needed. A texture the pack has no art for comes from a recipe: the pack has no mob but the player.

To check this tree, with no pack:

    tools/test-or-build.sh cmake --build --preset debug --target mc_tool_texture_pack
    build/debug/bin/tools/texture_pack verify --manifest textures/import.yaml --textures textures

## Recipes

The textures the pack lacks are ours: every mob but the player, gold tools, armour, deep rock, more trees and plants, the animals' meat, hides, egg and milk, the buttons, the font, chromium and stainless steel (plan 005 phase 11). `compile/src/texture_vector/recipe/README.md` lists the operations. A recipe reads only its own `index.yaml`, so these 284 textures survive the pack being removed. To change one, edit its yaml, then:

    tools/test-or-build.sh cmake --build --preset debug --target mc_tool_texture_vector
    build/debug/bin/tools/texture_vector recipes --textures textures

That writes its `index.mctex` and `README.md`, which are committed. To check that everything is whole, that each recipe still makes what is committed, and that everything the game needs is here:

    build/debug/bin/tools/texture_vector check --textures textures --materials textures/vector.yaml

A stage that wants a new texture adds it under `needs` in `vector.yaml`, and gives it a source. Mobs share the player's 64 by 32 layout: a box of width w, height h and depth d at (x, y) has its top at (x+d, y), its bottom at (x+d+w, y), and in the next row right, front, left and back at (x, y+d), (x+d, y+d), (x+d+w, y+d) and (x+d+w+d, y+d). The head is 8 by 8 by 8 at (0, 0), the body 8 by 12 by 4 at (16, 16), an arm 4 by 12 by 4 at (40, 16) and a leg 4 by 12 by 4 at (0, 16). The animals of plan 005 phase 4 (`cattle`, `sheep`, `pig`, `goat`, `mountain_goat`, `chicken`) have a 128 by 64 skin each, male and female on the one, laid out for their own boxes in `config/mobs/`: a part of size [forward, left, up] is unwrapped with forward as its depth, left as its width and up as its height. The skeleton's limbs are 2 by 12 by 2 at the same places, and the creeper's four feet 4 by 6 by 4 at (0, 16).

## Vector data

`texture_vector vectors` derives palette, mean colour, height, normal, emissive map and alpha class for each texture, into `build/<preset>/generated/`, never here: `compile/src/texture_vector/analyze/README.md` says how. What pixels cannot say, such as which textures shine, is in `vector.yaml`, which a re-import does not touch.

## Into the game

Every build turns this directory into one blob, `build/<preset>/generated/textures/index.mctb`, holding each texture's mip chain and vector data, and links it into the program. `compile/src/texture_blob/mip/README.md` has the rules for mips, and `run/src/texture/README.md` says how the game reads it. Change a texture or `vector.yaml`, build, and it is in.

## Re-importing

Only to change what is imported, or to move the pin. The pack is untrusted, so clone it to its own directory, and never run anything from it. Check out the commit that `pack.commit` names, or change that line after reading the new commit's licence.

    git clone https://github.com/minetest-texture-packs/Pixel-Perfection /tmp/mc_c_assets/pixel_perfection
    build/debug/bin/tools/texture_pack import --pack /tmp/mc_c_assets/pixel_perfection \
        --manifest textures/import.yaml --textures textures

It writes nothing unless the commit, the licence and every file check out, and it changes only files whose bytes differ, so `git status` shows what the import really changed. It replaces each imported `index.yaml`, `README.md` and `index.mctex`, and `ATTRIBUTION.md`. Keep anything hand-written out of those files, or put it back after. `ATTRIBUTION.md` has one such part: its section "What this file cannot promise" and the sentence at its top that points there, which the tool does not write, so `git diff` shows them going and they must be restored. It removes nothing: a texture dropped from `import.yaml` has to be deleted by hand, and `verify` will say which.

## Minecraft's default textures

Plan 005 phase 9: a person can play with Minecraft's own textures on their own machine. **They are Mojang's, and never committed**: this repository is public. Pixel Perfection stays here, unchanged, and is what every fresh clone and every other machine builds with.

```
textures/                          committed: Pixel Perfection and our recipes
textures/import_minecraft_default.yaml   committed: names only, ours against Minecraft's
textures_local/                    ignored by git: the same tree, with Mojang's art where the map says
```

To make and use it:

    tools/fetch_minecraft_default.sh
    tools/test-or-build.sh cmake --build --preset debug --target mc_tool_texture_pack
    tools/import_minecraft_default.sh
    tools/test-or-build.sh cmake --preset release
    tools/test-or-build.sh cmake --build --preset release

The first downloads the pinned client jar from Mojang's servers, as the launcher does, checks its SHA-1, and unpacks only `assets/minecraft/textures/` to `/tmp/mc_c_assets/minecraft_default/`. Nothing from the jar is run. The third copies `textures/` to `textures_local/` and runs `texture_pack import-default`, which `compile/src/texture_pack/default_pack/README.md` describes. Configure prints `Textures: textures_local/...` or `Textures: textures/...`, and the build uses that tree. Delete `textures_local/` to go back.

The map names every texture here once: a file from the pack, ten files stacked as the crack's frames, or `keep` ours with the reason, for what Minecraft has no picture of in our size or layout: beds, chests and doors drawn as models there, our bench, most screens. Grass, leaves, vines and water are grey in Minecraft and coloured by biome when drawn. With no biome tint in our renderer, the import bakes one colour per kind.

Nothing under `textures_local/` may be tracked: `cmake/check_textures_local.cmake` fails the build if it is. With it in use, the tests whose expected pictures or digests come from the committed art are disabled: `MC_ART_TESTS` in `cmake/targets.cmake`. Their pictures are never regenerated from it.
