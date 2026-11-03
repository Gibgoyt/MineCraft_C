# 001-ahmed-bohemoth-minecraft-from-scratch — phase 18, stage 3: Documentation audit

**Status:** done, but for the licence decisions only Ahmed can make, in "For Ahmed" below. What the audit could and could not check is in issue 1   (updated 2026-11-04)

## Goal

Every README is accurate, REPO_STRUCTURE matches the tree, all todos are truthful, and licences are complete.

**Wave:** W10. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-18-release-hardening.md`.

**Owned paths** (edit only these, plus this file):

- All `README.md` files
- `plans/REPO_STRUCTURE.md`
- `textures/ATTRIBUTION.md`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Compare every `README.md` against its code: by script for all, and by reading where a script flagged something or a sample was taken: issue 1
- [x] Compare `plans/REPO_STRUCTURE.md` against the tree
- [x] Check every phase and stage todo against what actually landed: the names, paths, tests and commits they cite, not by re-running each stage: issue 5
- [x] Check licences and attribution for all imported assets. The audit is done and written into `textures/ATTRIBUTION.md`; the decisions it leads to are Ahmed's: issue 6

## Verification

    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    find run compile cuda asm -type f -name 'index.*' | wc -l
    git status --short

Run on 2026-11-04, after a full `debug` build: `standards_lint` passed, the `find` counted 354, and `git status --short` was clean after the commits below. Also passing: `compile_texture_pack`, `compile_texture_vector`, `clang_format`. The rest of ctest was not run, as this stage changed no code.

## Commits

Appended as they land:

- `07a239a` Audit the documentation against the tree: bring stale READMEs and the repository map up to date, and say in the attribution what the pack's history leaves unproven.

## Issues found

All 2026-11-04, by this stage.

1. **What "compare every README" came to.** Expected: each README read against its code. Actual: 1112 READMEs. 516 under `textures/` are generated and held by tools, `texture_pack verify` and `texture_vector check`, both passing. About 285 under `tests/compile/standards_lint/fixtures/` are lint fixtures, not documents. The other 311 are 767 KB, too much to read line by line in this stage, so one-off scripts, not committed, checked them all: every field the standard requires, tier against depth, title against path, every backticked path, identifier, constant, type and `--flag` against the tree, every `ctest` name against `ctest -N`, every function in a module's header named in its README, every system a module's `index.c` includes named under Depends on, every module including a header named under May be called by, struct sizes beside a size in the prose, each config group's yaml keys in its README, and stale wording (`yet`, `later`, `until Stage`, `will`, `for now` and more). Everything flagged was read and checked against code. A sample of READMEs with nothing flagged, read against their headers, was right. **Not checked:** prose claims with no name a script can test, such as behaviour, costs and thread rules. Some may still be wrong.
2. **Stale READMEs: fixed, in 65 files.** Written while later stages were still to come and not updated when they came. The kinds, each checked against code:
   - **Saving.** `run/src/item/`, `item/equipment`, `entity`, `mob/spawn`, `render/lod`, `settings` and `ui/menu` said nothing is saved until Stage 16. `run/src/save/state/` and `save/world` do it.
   - **Other arrivals.** `entity/item_drop` said items are not drawn and a hand is one slot; `player/interaction` said there is no crack; `item` said the right button cannot open a chest; `ui/menu` that Settings is unanswered; `terrain/ore` that nothing fills a world; `mob/creeper` that other kinds stay in the store with no health; `tests/run/types` that no mode opens a display.
   - **Callers and dependencies.** 35 "May be called by" lines still said "later" or left out real callers, and 9 "Depends on" lines left out what `index.c` includes.
   - **Functions no README named.** `display_flip_vsync_set`, the pattern cursor's three, `item_drop_spawn_worn`, `item_world_edits_read`, `equipment_armor_toughness`, `player_sneak_sent`, `render_entities_deinit`, `render_frame_fog_set`, `render_frame_present_full`, five of `traverse`'s, and every `simd_reference_*`.
   - **Left alone on purpose.** The audit's remaining hits are false alarms: short forms such as `set_center`, section names such as `ore/dirt`, generated headers, and `fuzz_*`, which exist only in the `sanitize` preset. So is `tests/run/ring/` in `tests/run/thread/README.md`, which says that it does not exist. `shade_box_light` is an inline helper in a shared header, with no README line.
3. **`plans/README.md` described another project.** Expected: this repository's rules. Actual: `npm test`, `npm run migration:test`, `data/fastlane-db/README.md`, and an opening about "pre-admission work" and "Reception". None of those exist here. **Done:** those lines now give this repository's commands, and the naming section shows `-phase-NN-stage-M-` as plan 001 uses it. The four rules are unchanged. This is Ahmed's process document, so `git diff` on it is worth his look, and it reverts cleanly.
4. **`plans/REPO_STRUCTURE.md`: small differences, fixed.** Every path resolved, and each system's parts matched the tree. Fixed: the opening "written before any code exists"; `player/combat` listed twice; "Parts so far"; `game` "will borrow" `job`; `profiler` missing from section 11's diagram, where the lint already places it (`compile/src/standards_lint/rules_include/index.c`); no mention that headers do not always mirror sources; `tests/asm/support/` and two more unlisted. Tests covered by a general row such as `tests/run/<system>/<part>/` were left to it.
5. **Phase and stage todos: no false tick found, within what was checked.** Checked by script over all 102 plan files: every sha under Commits is in the history; every path, ctest, identifier and flag in a ticked todo exists, or belongs to a spike or a one-off that the plan file holds; Status against boxes; a parent's boxes against its stages' Status. **Not checked:** whether each ticked behaviour still holds. That takes re-running each stage's tests, which this stage's Verification does not ask and did not do. Left alone, as these paths are not this stage's, and for whoever owns them:
   - `phase-18-release-hardening.md` says "not started", while its stages have started, and its boxes wait for the gate.
   - `plans/CODING_STANDARD.md` line 8 still says "written before any code exists".
   - `run/include/main/index.h`'s comments on `app_t` are out of date: `/* only --walk moves anything with it */`, `/* takes nothing until --test-world opens a world */` and others, as the default run is `--menu`.
   - `run/src/main/index.c:654` logs "no game systems exist yet" in the no-terminal check, and `tests/run/types/index.c` holds it to that. The words are out of date, the behaviour is as documented.
   - `run/src/player/camera/`: `player_camera_outline_draw` and the sim's casts for `edge_visible` remain unused by any picture, as its README and Stage 09.5 issue 2 say.
6. **Licences. The file's claim went further than the evidence: corrected.** Expected: `ATTRIBUTION.md` true. Actual: it said flatly "No Mojang textures ... are used anywhere", while Stage 06.2's issue 12 found the pack's own commits saying 6 imported textures came from other packs with Minecraft in their names. **Done:** the sentence is now limited to what is known, and a new last section, "What this file cannot promise", names the 6, the missing licence file, and two more things the table did not say: every build derives mips and vector data from these textures, and 50 golden PNGs under `tests/` are renders, of a game that draws with them. **Evidence:** `texture_pack verify` and `texture_vector check` pass, 325 and 186; 325 table rows; `grep -c 'kind: pack'` 325 and `kind: procedural` 186; the clone has 414 files: the 413 `ATTRIBUTION.md` counts, every one named in `import.yaml`, and `.gitignore`. `.git/refs/heads/master` there is `7935d064...`, the manifest's pin. No other art is imported: every PNG, DEFLATE and zlib file in the tree is a golden render or from a committed `generate.py`, and PngSuite was never committed. **Weakness left:** `compile/src/texture_pack/output/index.c` writes this file and does not know of the hand-written part, so a re-import removes it. `textures/README.md` says so. The tool is not this stage's to change. Teaching it would make `import.yaml` hold the text.
7. **The lint needs a full build.** Expected: `ctest -R '^standards_lint$'` to run after building only the lint. Actual: it failed with `cannot open .../mc_run.dir/./run/src/arena/index.c.ci` and so on: the recursion check reads what compiling everything leaves. Not a defect, and no README or code was at fault. **Done:** the whole debug preset was built under the lock, then it ran. Verification below has the result.
8. **Process.** The first `cmake --preset debug` here, and `ctest -N`, ran without the mutex, though configuring builds the machine tool; later commands used it. `texture_pack verify` and `texture_vector check` ran bare, under `timeout`, as they are a second each. The worktree isolation check refused several compound commands, and they were re-run as plain ones.
9. **Nothing at the repository's root says what this is.** There is no root `README.md`: `plans/REPO_STRUCTURE.md` does not list one and the lint does not ask for one. Left alone, as adding a document is not auditing one. Worth having before a release.

## For Ahmed

Licence decisions, carried over from Stage 06.2's "For Ahmed", whose commands still apply:

1. Does the pack's stated CC-BY-SA-4.0, with no licence file and no per-file provenance, satisfy you?
2. Do the 6 textures stay? `blocks/coral_{brown,cyan,green,orange,pink}` and `blocks/berry_bush_leaves`. If not, Stage 06.2's "For Ahmed" has the steps, and the 6 rows and the section in `textures/ATTRIBUTION.md` change with them.
3. Before giving anyone the program: `textures/ATTRIBUTION.md` has to go with it. Nothing packages a release, so nothing does this for you.

