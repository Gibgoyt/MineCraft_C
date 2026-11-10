# tests/run/mob/animal/proof

**Purpose:** ctest `run_mob_animal_proof`, named by its path. The scripted headless proof of plan 005 phase 4 stage 4: the six animals in a whole real game, through `run/src/game/` and its real buttons, written as pictures for a person to check by eye.
**Tier:** test.
**Owns:** nothing outside the test arena, blocks on the card for the length of the test, and the pictures it writes.
**Lifetime:** One run.
**Threads:** main and the real workers, through `game_settle` and the headless scene's ticks; no sim or gpu thread.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/game/` and everything under it, `run/src/display/headless/png/` to write the pictures, and a card, compute only. No display, console or input device.

The pictures go to `MC_ANIMAL_PROOF_DIR`, or `/tmp/mc_animal_proof/`, which is kept: each run writes the same names over the last. At noon on day 10, so that herds do not come every tick, on a field of grass put down about the body:

- `herd_cattle.png`, `herd_sheep.png`, `herd_pig.png`, `herd_goat.png`, `herd_mountain_goat.png`, `herd_chicken.png`: four of each, side on, male, female, male, female from left to right as the eye sees them. The males are bigger, with horns (bull, ram, billy), tusks and a dark bristly coat (boar), a beard (billy) or a tall comb and a dark tail (rooster).
- `sheep_shorn.png`: shears on the ewe in front, with the right button. She gives 1 to 3 white wool and shows her skin; the shears wear by 1. A second press gives nothing. The ram behind her still has his wool.
- `cow_milked.png`: a bucket on a bull gives nothing; on a cow it comes back a milk bucket, and holding the button on does not drink it.
- `bed_of_wool.png`, `bed_slept_in.png`: three white wool over three oak planks at a crafting table make a bed, and wheat no longer does; it is put down on the grass, and at 20:24 the right button on it puts the body to sleep.

It checks what it can in numbers. Whether the pictures look right is for a person: read them.
