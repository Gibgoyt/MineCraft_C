# 001-ahmed-bohemoth-minecraft-from-scratch — phase 6: Textures

**Status:** not started   (updated 2026-11-01)

## Goal

Open-licensed and generated textures compile into our own format, with vector data per texture, and are embedded in the binary through `incbin`. Builds need no network.

**Wave:** W4.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-stage-1-inflate-png.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-stage-2-texture-pack-import.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-stage-3-texture-vector.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-stage-4-embed-runtime-lookup.md`

## Todo

- [ ] Stage 06.1: Inflate and PNG decoder
- [ ] Stage 06.2: Texture pack import
- [ ] Stage 06.3: Texture vector data and recipes
- [ ] Stage 06.4: Embedding and runtime lookup
- [ ] Phase gate passed

## Verification

**Gate:** All four stages pass; `textures/ATTRIBUTION.md` is complete; a clean build works with no network. Default pack: Pixel Perfection, repo states CC-BY-SA-4.0, verified in 06.2. VoxeLibre media is not bulk-imported. No Mojang jars and no decompiled code.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(compile_(inflate|png|texture)|run_texture)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None.
