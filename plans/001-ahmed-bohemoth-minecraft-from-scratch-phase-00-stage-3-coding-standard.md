# 001-ahmed-bohemoth-minecraft-from-scratch — phase 0, stage 3: Coding standard document

**Status:** done   (updated 2026-11-01)

## Goal

`plans/CODING_STANDARD.md` holds plan section 4 in full, all type rules, and the NASM and CUDA ABI rules, covering requirements R19 to R25.

**Wave:** W0. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-00-documentation.md`.

**Owned paths** (edit only these, plus this file):

- `plans/CODING_STANDARD.md`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Write the memory law from plan section 4 in full, including the One-Owner Principle
- [x] Write the banned and allowed lists from plan Phase 00, targeting gcc 16 and nvcc 13
- [x] Write the layout rules: written-out padding, `-Werror=padded`, `static_assert`s, no `packed`
- [x] Write the 2000-line hard limit, with the split point at 1500
- [x] Write the NASM rules (SysV ABI, `default rel`, `vzeroupper`, `.note.GNU-stack`, batch APIs only) and the CUDA rules (C-like subset, `extern "C"` boundary)

## Verification

    grep -nE 'uint32_t|float32_t|-Werror=padded|2000|vzeroupper|extern "C"|One-Owner' plans/CODING_STANDARD.md
    # Manually confirm every R19 to R25 rule appears.

## Commits

Appended as they land:

## Issues found

None.
