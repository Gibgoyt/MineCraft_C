# 001-ahmed-bohemoth-minecraft-from-scratch — phase 0, stage 4: Speed research write-up

**Status:** done   (updated 2026-11-01)

## Goal

`research/003-speed-research.md` holds plan section 3 with sources, marking each claim as sourced or engineering judgment.

**Wave:** W0. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-00-documentation.md`.

**Owned paths** (edit only these, plus this file):

- `research/003-speed-research.md`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Write up every row of the plan section 3 table in `research/003-speed-research.md`
- [x] Mark each claim as *sourced* or *engineering judgment*
- [x] Include every source link from plan section 3
- [x] State plainly that Sodium details were not confirmed by the searches

## Verification

    grep -c 'sourced\|engineering judgment' research/003-speed-research.md
    # Manually compare the write-up against plan section 3.

## Commits

Appended as they land:

- `7e615ac` Document the full plan, repository structure, coding standard and speed research for the from-scratch game.
## Issues found

### Several speed claims could not be confirmed

- **Expected:** primary sources for each technique in plan section 3.
- **What actually happened:** searches did not confirm Sodium's own vertex format, multi-draw or cave-culling details, Minecraft's random-number generator, Minetest Game's texture licence, or current brickmap ray rates. Lithium, C2ME and FerriteCore were described only by listing pages, not developer documents.
- **Evidence:** searches on 2026-11-01, listed under Sources in `research/003-speed-research.md`.
- **What was done:** each claim is marked sourced or engineering judgment, gaps are stated plainly, and none of them blocks the plan because Sodium's techniques do not apply and the rest are measured in Phases 01 and 17.
