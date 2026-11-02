# 001-ahmed-bohemoth-minecraft-from-scratch — phase 6: Textures

**Status:** in progress: every agent-runnable check passes. The licence review in Stage 06.2 awaits Ahmed, and so the gate does   (updated 2026-11-02)

## Goal

Open-licensed and generated textures compile into our own format, with vector data per texture, and are embedded in the binary through `incbin`. Builds need no network.

**Wave:** W4.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-stage-1-inflate-png.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-stage-2-texture-pack-import.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-stage-3-texture-vector.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-stage-4-embed-runtime-lookup.md`

## Todo

- [x] Stage 06.1: Inflate and PNG decoder
- [ ] Stage 06.2: Texture pack import. Tool, import, attribution and coverage are verified; the licence awaits Ahmed
- [x] Stage 06.3: Texture vector data and recipes
- [x] Stage 06.4: Embedding and runtime lookup
- [ ] Phase gate passed. The gate commands all pass; "verified in 06.2" for the licence is the part that awaits Ahmed

## Verification

**Gate:** All four stages pass; `textures/ATTRIBUTION.md` is complete; a clean build works with no network. Default pack: Pixel Perfection, repo states CC-BY-SA-4.0, verified in 06.2. VoxeLibre media is not bulk-imported. No Mojang jars and no decompiled code.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(compile_(inflate|png|texture)|run_texture)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

**Gate check, 2026-11-02.** Each defect is logged in its stage file; this is the list.

| Expected | Actual | Done | Where |
|---|---|---|---|
| A licence verified when its box is ticked | No licence file exists, and the pack's history says 6 imported textures came from Minecraft-era packs (`28ab9a0`, `7725aa8`) | Box unticked, escalated | 06.2 issue 12 |
| 14 light rows give 14 lights | `blocks/tnt_top_burning` stayed dark, silently | Row fixed; the tool now refuses such a row | 06.3 issue 15 |
| Compositing's rounding held by a test | A `+ 128u` mutant survived | Test added | 06.2 issue 14 |
| Pointer fields named `borrowed_` | Missing in the public texture API and in the tools' tables, and the lint cannot see it | Renamed | 06.2 issue 15, 06.3 issue 17, 06.4 issue 16 |
| `needs_ahmed` commands findable | Referred to in stage files, and written in none | "For Ahmed" sections added | 06.2 issue 13, 06.3 issue 16 |
| This file current | `not started`, with every stage claiming done | Updated | here |

**Results, on the final tree:** a clean build of `debug` with no network (`unshare -rn`, in which only a downed `lo` exists); the gate's ctest line, and all of `ctest --preset debug`; the gate's tests in `sanitize` and `release`; `textures/ATTRIBUTION.md` complete, with a row for each of the 325 `source.kind: pack` textures and none for the 186 recipes; no VoxeLibre media and no `FetchContent` or the like in the build.

**Process.** The worktree isolation check refused several compound shell commands, and I re-ran each as plain separate commands, with nothing bypassed. One refusal also dropped an `rm -rf build`, so my first "clean" build was not clean: its 0.3 s configure and missing blob step gave it away. The results above are from the rerun, after `ls build` showed it gone. History was read through the GitHub API, not `git` on the clone, so Ahmed's commands in Stage 06.2 are still worth running.

**No Mojang jars and no decompiled code:** none in the repository, and nothing fetches any. Whether every pack texture is free of Mojang origin is the review above, and cannot be shown from here.

**Awaits Ahmed:** the licence review, Stage 06.2 "For Ahmed". Art review, Stage 06.3 "For Ahmed", which does not hold up the gate.
