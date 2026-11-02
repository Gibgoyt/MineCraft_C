# textures

Translated texture data, committed, so that a build needs no pack and no network. Licences and credits are in `ATTRIBUTION.md`, which is generated and ships with anything that ships these textures.

```
textures/
├── ATTRIBUTION.md             every source pack, author, licence and commit
├── import.yaml                what the import takes from the pack, and why it leaves out the rest
├── blocks/dirt/
│   ├── index.mctex            the pixels: see compile/src/texture_pack/mctex/README.md
│   ├── index.yaml             where it came from, and its size
│   └── README.md              its credit, and what was changed
├── items/
├── mobs/
└── ui/
```

Groups: `blocks`, `items`, `mobs`, `ui`. Every directory below a group is one texture, and has an `index.yaml` and a `README.md`.

`source.kind` in `index.yaml` says where a texture comes from. `pack` means imported, and checked against `import.yaml`. Anything else, such as a procedural recipe from Stage 06.3, is its own stage's business, and owes no credit. **No Mojang textures and no decompiled code**, ever.

## Without the pack

The game and its build read only this directory. Nothing in the build refers to the pack, so it may be missing, and a network is never needed. A texture the pack has no art for comes from a recipe: the pack has no mob but the player.

To check this tree, with no pack:

    tools/test-or-build.sh cmake --build --preset debug --target mc_tool_texture_pack
    build/debug/bin/tools/texture_pack verify --manifest textures/import.yaml --textures textures

## Re-importing

Only to change what is imported, or to move the pin. The pack is untrusted, so clone it to its own directory, and never run anything from it. Check out the commit that `pack.commit` names, or change that line after reading the new commit's licence.

    git clone https://github.com/minetest-texture-packs/Pixel-Perfection /tmp/mc_c_assets/pixel_perfection
    build/debug/bin/tools/texture_pack import --pack /tmp/mc_c_assets/pixel_perfection \
        --manifest textures/import.yaml --textures textures

It writes nothing unless the commit, the licence and every file check out, and it changes only files whose bytes differ, so `git status` shows what the import really changed. It replaces each imported `index.yaml`, `README.md` and `index.mctex`, and `ATTRIBUTION.md`. Keep anything hand-written out of those files, or put it back after. It removes nothing: a texture dropped from `import.yaml` has to be deleted by hand, and `verify` will say which.
