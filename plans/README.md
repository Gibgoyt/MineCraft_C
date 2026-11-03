# `./plans/` — how planned work is recorded in this repository

Plan mode writes its plan to `~/.claude/plans/<slug>.md`: outside the repo,
invisible to everybody else, and gone when the machine is reimaged. Work done
from a plan that lives only in a home directory leaves nothing here to say what
the plan had been, which phase was finished, or what went wrong on the way.

**So plans are repository artifacts.** They live in this directory, they are
split one file per phase, and each phase file carries a live todo list that
whichever agent is working keeps current.

This README is the authoritative description of that arrangement. An agent
picking up planned work in this repo reads this file first.

---

## The four rules

### 1. On approval, the approved plan is copied into `./plans/` — VERBATIM

The moment the user approves a plan, the file at `~/.claude/plans/<slug>.md`
is copied into this directory under its numbered name.

**Verbatim means verbatim.** The copying agent does not summarise it,
re-order it, drop a section it judges redundant, fix its prose or "bring it
up to date". The point of keeping it is to know what was actually agreed,
including the parts that later turned out to be wrong — a plan edited into
agreement with what happened cannot tell anybody that the plan was wrong, and
that is usually the most useful thing in the file.

Corrections go in the phase files' **Issues found** sections, where they are
dated and attributed to the step that discovered them.

### 2. One file per stage/phase, named to sort

```
./plans/NNN-<author>-<round>-{plan-name}.md                  the verbatim plan
./plans/NNN-<author>-<round>-{plan-name}-phase-1.md          phase 1
./plans/NNN-<author>-<round>-{plan-name}-phase-2.md          phase 2
```

A big plan splits a phase into stages, as plan 001 does, and the phase file then tracks its stages:

```
./plans/NNN-<author>-<round>-{plan-name}-phase-02-build-skeleton.md
./plans/NNN-<author>-<round>-{plan-name}-phase-02-stage-1-cmake-mutex-hello.md
```

- `NNN` is a zero-padded serial — `001`, `002`, `003` — allocated when the
  plan is approved. It never changes and is never reused, so the directory
  listing reads in the order the work actually happened.
- `{plan-name}` is a short kebab-case name for the work, and it may be split
  across several words to make each phase legible:
  `001-ahmed-r2-conquer-the-world-phase-1.md`.
- Every file of one plan shares the same `NNN` and the same `{plan-name}`.
  The suffix is the only thing that varies.

A phase is a unit of work that ends somewhere worth stopping — typically one
commit, or one tightly related group of commits. "Phase" is not a synonym for
"step": a phase that is one checkbox long should have been a checkbox in
another phase.

### 3. Every phase file is todo-tracked, and the todos are updated AS THE WORK LANDS

Each phase file carries a checklist. A box is ticked when the thing is done
and verified — not when it is started, and not in a batch at the end.

This applies to **the main thread and to every agent launched with edit
permission that implements or tests anything**. An agent that finished its
step and left the checklist stale has not finished its step. The checklist is
how the next agent — or the user, reading over your shoulder — knows where the
work actually is, and a stale one is worse than none because it is believed.

### 4. Issues are documented thoroughly, in the phase file, when they arise

Not deferred to a final summary, not left in a terminal scrollback that
nobody will read again. Each issue gets:

- **what was expected**, and what actually happened;
- **the evidence** — the command, the error text, the file and line, the row
  counts, the sha;
- **what was done about it** — fixed, worked around, left alone, or escalated
  to the user, and why.

A product bug found while doing something else is exactly the thing this
section exists for. Write it down even when it is out of scope — especially
then, because an out-of-scope bug has nowhere else to go.

---

## The approval handoff

**The first thing that happens after a plan is approved — before any
implementation — is a dedicated agent that sets up the plan files. The main
thread waits for it to finish before executing anything.**

That agent:

1. Allocates the next `NNN` by listing this directory.
2. Copies `~/.claude/plans/<slug>.md` to
   `./plans/NNN-<author>-<round>-{plan-name}.md`, byte for byte.
3. Creates one phase file per stage in the plan, from the template below,
   with every checkbox unticked and the status `not started`.
4. Reports the paths it created.

It is a separate agent, and the main thread blocks on it, for one reason: the
setup must be complete and consistent before the first edit lands. An agent
that starts implementing phase 1 while phase 3's file does not yet exist will
write its findings somewhere else, and the record is then wrong from the first
commit.

---

## The phase-file template

```markdown
# NNN-<author>-<round>-{plan-name} — phase N: <title>

**Status:** not started | in progress | blocked | done   (updated YYYY-MM-DD)

## Goal

One paragraph: what is true when this phase is done. Written so somebody can
tell whether it has been achieved without reading the todo list.

## Todo

- [ ] A concrete step, naming the file it touches
- [ ] Another, with the command that proves it
- [ ] ...

## Verification

The exact commands that prove this phase, with the mutex wrapper spelled out:

    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_world_section$' --output-on-failure

Never an unwrapped heavy command. See `.claude/rules/test-or-build-mutex.md`.

## Commits

Appended as they land:

- `<sha>` <subject>

## Issues found

What went wrong, what the plan got wrong, and any product bug discovered on
the way. See rule 4 for what each entry must contain. An empty section is
fine — write "None." — a missing section is not.
```

---

## Conventions this directory inherits

- **Heavy commands go through the mutex.** Every build and test runs as
  `tools/test-or-build.sh cmake --build --preset <preset>` or
  `tools/test-or-build.sh timeout <seconds> ctest --preset <preset> ...`. A phase
  file's Verification section that lists a bare `ctest` is a bug in the phase file.
- **Commit messages are a sentence**, in the style already in the log, with no
  tag prefix and no attribution trailer. See `plans/CODING_STANDARD.md` section 11.
- **Plans are not a substitute for the code's own comments.** This directory
  records what was decided and what happened; a rule that a future reader of
  a source file needs belongs in that file's header, where they will be
  looking.

