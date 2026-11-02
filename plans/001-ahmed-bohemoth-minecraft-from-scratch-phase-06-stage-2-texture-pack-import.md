# 001-ahmed-bohemoth-minecraft-from-scratch — phase 6, stage 2: Texture pack import

**Status:** in progress: everything agents can run passes, and only the licence review below awaits Ahmed   (updated 2026-11-02)

## Goal

Import works from a pinned open pack cloned to `/tmp/mc_c_assets/`, with a licence check and `ATTRIBUTION.md`. Outputs are committed, so builds need no network.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-06-textures.md`.

**Owned paths** (edit only these, plus this file):

- `compile/{src,include}/texture_pack/`
- `textures/**`
- `tests/compile/texture_pack/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Clone Pixel Perfection at a pinned commit into `/tmp/mc_c_assets/pixel_perfection/`, and treat it as untrusted data. Pin: `7935d064fc6f993d1b5038ed5ec17a615600cf0a` (2023-06-30, branch `master`), in `textures/import.yaml`; the tool refuses any other commit
- [x] Verify per-pack coverage, and record the commit and findings here: issue 2
- [ ] Verify the licence. **Awaits Ahmed.** The pack has no licence file, and its history says some art came from another pack: issues 1, 12 and "For Ahmed". The import stays usable meanwhile
- [x] Implement `compile/src/texture_pack/index.c`, translating the pack into `textures/<GROUP>/<dir>/` for groups `blocks`, `items`, `mobs`, `ui`
- [x] Give each texture directory an `index.yaml` and `README.md`
- [x] Write `textures/ATTRIBUTION.md`
- [x] Register test `compile_texture_pack`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^compile_texture_pack$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    grep -n 'CC-BY-SA-4.0' textures/ATTRIBUTION.md

All run on 2026-11-02, and all passed. `pgrep` printed nothing, and the `grep` found line 12. Beyond this list, also passing: `clang_format`, and `compile_texture_pack` in the `sanitize` and `release` presets.

## Commits

Appended as they land:

- `64fa317` Import the pinned Pixel Perfection pack into textures through our own checked tool, with attribution, and a test that needs no pack.

## Issues found

All 2026-11-02, by this stage.

1. **The pack has no licence file. Escalated to Ahmed; not blocking.** Expected a `LICENSE` file, as the plan asks to verify one. Actual: none anywhere in the tree (`find` for `*licen*`, `*copying*`), and the GitHub API reports `"license": null`. What the pack does say, in three places: `texture_pack.conf` has `license = CC-BY-SA-4.0`, `README.md` has "Creative Commons Attribution-Share Alike 4.0 International License", and the repository description says the same. All agree and name one licence, so I read this as stated, and imported. It is weaker than a licence file in two ways that only Ahmed can weigh:
   - **No per-file provenance.** Nothing says who drew which file. The README credits Hugh "XSSheep" Rutland and three contributors, and the pack does not say whether any file came from elsewhere. By eye, on a contact sheet of `default/` and `Template.png`, the art is a consistent original style, which is not proof.
   - **The plan's own rule is no Mojang textures.** From memory, and not checked, the same author's pack of the same name began as a Minecraft resource pack. That would not make the art Mojang's, but this repository cannot show it either way.

   **What was done:** the tool holds the pack to that statement. It refuses to import if either statement changes, if `texture_pack.conf` states another licence, or if any licence-like file nobody reviewed appears. The textures remain removable: they are all in `textures/`, and Stage 06.3's recipes are the plan's fallback. The exact review for Ahmed is in this stage's `needs_ahmed`.
2. **Coverage.** 413 files in the pack: 325 imported, 86 skipped with a reason in `textures/import.yaml`, 2 licence evidence (the tool refuses a pack with any file unaccounted for). The only things under `mobs` are the player skin and three cart images. **Gaps for Stage 06.3:** no mob at all except the player skin (no zombie, skeleton, creeper, or any animal); no gold tools; no raw iron icon; no cooked food beyond bread, and no meat; no armor. Pack-specific oddities: `simple_potions/` is 11 symbolic links into `vessels/`, which are skipped, since the tool never follows a link. `default/default_torch.png`, `default/iron_block.png`, `tnt/tnt_top_burning_animated.png` and the four `default/default_rail*` files are pixel-identical to files imported under other names (compared with ImageMagick); `default_lava.png` is exactly frame 0 of the animated strip. `default_leaves_simple.png` and `default_furnace_front_active.png` differ a little from what was imported, so they count as lesser copies, not exact ones.
3. **Stage 06.1's question about the stricter decoder is answered for what was imported.** Its issue 4 said Pixel Perfection might be refused by a decoder stricter than libpng. Actual: all 325 imported PNGs decode, including the two also used as bases. The 68 skipped regular PNGs were not put through it, because the tool does not read skipped files.
4. **Imported pixels were compared with an independent decoder.** A one-off script, not committed, compared each committed `index.mctex` with ImageMagick's decode of the pinned pack: 317 of 317 uncomposited textures byte-identical. For the 8 composited ones (5 ores, grass, dry grass and snow sides) all within 1 level of ImageMagick's own `-composite`, whose rounding differs. The test also holds a composite worked by hand. I looked at a contact sheet of about 35 results, including every composite: they look right.
5. **A bug of mine that the test caught: garbled error text.** Expected `frames: four` to report the line and the type. Actual: `import.yaml:8:8: Q`J` and raw bytes. Cause: `yaml_error_write` in `compile/src/yaml/index.c` keeps the first error by writing only if `error->line == 0`, so a `yaml_error_t` on the stack must start zeroed, and mine did not. Fixed: every such local in `texture_pack` is `= {0}`, and the test now requires the full message with `15:48`. **Out of scope and left alone:** `compile/include/yaml/index.h` does not state this rule, and the next caller can make the same mistake. `grep -n 'yaml_error_t error;'` lists any others. Worth one sentence in that header.
6. **Two wrong expectations in my first test, both the test's fault.** `from: a/gone.png` reports `a/dirt.png` as unaccounted first, which is correct, so the expected text changed, and a new case deletes the file to cover "the pack does not have". And I had miscounted a column (45 for 48).
7. **Tests checked by breaking the tool on purpose.** A one-off script, not committed, made one-line changes across all six parts, 39 runs, some repeated because a first attempt did not compile: `O_NOFOLLOW` and `O_NONBLOCK` dropped, the pin, licence, coverage, name, size, opacity and hash checks removed, compositing's rounding, the check pass before writing, and more. All were caught, and none survived. Each was restored by the script, and the final build and test ran after it.
8. **What the pin does not prove,** left alone and stated in `compile/src/texture_pack/README.md`: it checks the clone's `HEAD`, not its working files. A clone edited at the right commit would import, and the changes would show as a diff against the committed `textures/`.
9. **Choices the plan left open:**
   - A re-import replaces each imported `index.yaml`. `verify` allows extra keys there, so Stage 06.3 may add them, but anything it adds to an imported texture is lost on a re-import. `textures/README.md` says so.
   - Names are this game's, not the pack's (aspen is birch, pine is spruce, papyrus is sugar cane, steel is iron). Stage 07.1 can rename a row in `textures/import.yaml`, but a rename also needs a re-import and the old directory deleted, since an import removes nothing. `verify` reports the stale one.
   - The sun and moon went in `ui/`, and carts in `mobs/`, as the four groups have no better home.
   - The tool is not run by the build, so that a build needs neither the pack nor a network.
10. **ShareAlike reaches further than this stage.** `ATTRIBUTION.md` says it must ship with anything that ships these textures. Stage 06.4 embeds the textures in the binary, so any release needs that file beside it. Nothing enforces that yet, because nothing packages a release; Stage 18.3's licence audit is where it belongs.
11. **Process.** The worktree isolation check refused several compound shell commands as too complex to verify, each named in its message, and I re-ran them as plain separate commands, with the source files written by the Write tool. Nothing was bypassed. Because of that rule I did not run `git log` on the clone after the first look, so **I did not search the pack's history for a licence file that was later deleted.** The first command, before the refusal, only printed the last 3 commits. The commit came from it, and `.git/HEAD` is checked by the tool. Ahmed can run the search; the command is in `needs_ahmed`.

12. **Gate check, 2026-11-02: the licence is less clear than issue 1 says, and a box claimed more than was done.** Expected "verify the licence file" to be done when ticked. Actual: there is no licence file, so nothing was verified, and the box only recorded the escalation. It is unticked now. Issue 11 also left the history search undone. I did it through the GitHub API, since the isolation check limits `git` to this worktree:
    - `commits?path=` for `LICENSE`, `LICENSE.txt`, `LICENSE.md` and `COPYING` each return no commits, so no such file was ever at those paths. `README.md` has 5 commits, none about licensing by message.
    - **Commit `28ab9a0` (2023-05-10): "new textures: blueberries (original), corals (from PP MineCraft)".** It adds the five `default_coral_*` files, which we import as `blocks/coral_{brown,cyan,green,orange,pink}`.
    - **Commit `7725aa8` (2023-05-12), by its body:** `default_blueberry_bush_leaves` and `default_leaves_simple` are based on dark oak leaves from "MCr PP", recoloured after sweet berry bushes. `520e1de` changes the first one's palette "based on PP Legacy's sweetberry bush". We import that file as `blocks/berry_bush_leaves`. `default_leaves_simple` and `default_blueberry_overlay` are skipped.
    - So 6 imported textures state a Minecraft-pack origin. That is the "began as a Minecraft resource pack" worry in issue 1, now in the pack's own words for these files. It does not show that they are Mojang's, nor what licence those other packs carry. `ATTRIBUTION.md` still says no Mojang textures are used, and the repo cannot prove that for these 6.
    - **What was done:** left in place and escalated, since whether to keep them is a legal call. Only commit messages were read, 42 in all, so other files may have an unstated origin.
13. **Gate check: `needs_ahmed` pointed nowhere.** Issues 1 and 11 send the reader to "this stage's `needs_ahmed`", which was only the earlier agent's return value and is in no file. Fixed by "For Ahmed" below.
14. **Gate check: `compositing's rounding` was not held to the byte.** Issue 7 says it was caught. Actual: my mutant `+ 128u` in `tp_composite_over_opaque` survived `compile_texture_pack`, because no pixel of `TT_ORE` sums to a remainder of 127. Fixed: `compositing_rounds_to_nearest_on_each_side_of_a_half`; `+ 126u` and `+ 128u` are now both caught. The committed pixels were right all along, by an independent recomputation.
15. **Gate check: pointer fields without the `borrowed_` prefix** in `tp_entry_t`, `tp_evidence_t`, `tp_skip_t` and `tp_manifest_t` (CODING_STANDARD section 4; the lint cannot see this one). Renamed; no behaviour changes. Left as they are, and not called defects: `tp_arguments_t`-style structs holding `argv` strings, and tables of string literals, since neither points into a heap block.

Gate check re-runs, all passing: the Verification in `debug`; `compile_texture_pack` in `sanitize` and `release`; `texture_pack verify`; a real re-import from the pinned clone to a scratch copy, `0 output files changed`, with `diff -r` empty; every row in `ATTRIBUTION.md` against each `source.kind`, 325 and 186, none stale.

## For Ahmed

The licence review. Nothing here blocks the build, and only you can decide it.

    git -C /tmp/mc_c_assets/pixel_perfection log --all --full-history --stat -- '*LICEN*' '*COPYING*' '*NOTICE*'
    git -C /tmp/mc_c_assets/pixel_perfection show --stat 28ab9a0 7725aa8 520e1de

If the clone is gone, the same commits are at https://github.com/minetest-texture-packs/Pixel-Perfection/commit/28ab9a0 and so on. Decide two things:

1. Does the pack's stated CC-BY-SA-4.0, with no licence file and no per-file provenance, satisfy you?
2. Do the 6 textures in issue 12 stay? To drop them, move each row in `textures/import.yaml` to a skip with its reason, delete its directory under `textures/blocks/`, give it a recipe, re-import, and run `texture_vector check`. Then tick the licence box here, and Stage 06.2 and the gate in the phase file.
