# 001-ahmed-bohemoth-minecraft-from-scratch — phase 9: Renderer features

**Status:** not started   (updated 2026-11-01)

## Goal

The renderer has non-cube shapes, sky, shadows, water, AO, TAA, entities and LOD to R=512, and each effect can be switched and measured.

**Wave:** W7.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-1-non-cube-shapes.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-2-sky-shadows-light.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-3-water-glass.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-4-ao-taa-godrays.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-5-entities-outline.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-09-stage-6-lod-clipmaps.md`

## Todo

- [ ] Stage 09.1: Non-cube shapes
- [ ] Stage 09.2: Sky, shadows and stored light
- [ ] Stage 09.3: Water and glass
- [ ] Stage 09.4: AO, TAA and god rays
- [ ] Stage 09.5: Entity boxes and outlines
- [ ] Stage 09.6: LOD clipmaps
- [ ] Phase gate passed

## Verification

**Gate:** Each effect has a setting, a measured cost, and a golden image.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^render_feature_' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

### Stages overlap on `cuda/*/shade`, so they cannot run in parallel

- **Expected:** plan section 7 defines a wave as stages whose paths do not overlap.
- **What actually happened:** stages 09.1 to 09.4 all own `cuda/{src,include}/shade/`, and several also share `traverse` or `post`.
- **Evidence:** the Owned paths lists in the four stage files, compared during Stage 00.2 on 2026-11-01.
- **What was done:** 09.1, 09.2, 09.3 and 09.4 run one after another. 09.5 and 09.6 own separate paths and may run in parallel with them. The verbatim plan is unchanged.
