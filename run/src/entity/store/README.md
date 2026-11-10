# run/src/entity/store

**Purpose:** every body in a world with a mind of its own, one array per field, and how a body moves. What a mob is, is `run/src/mob/`'s; what it is made of and how it falls is here.
**Tier:** Arena. `entity_store_start` takes its arrays from the arena `entity` gives it, once per world. A tick, a spawn and a removal allocate nothing.
**Owns:** nothing. `entity_store_t` is a field of `entity_t`.
**Lifetime:** World.
**Threads:** `sim`, or whichever one thread holds the map. Nothing here is read from another thread: a worker that plans for a mob gets a copy of what it needs.
**Public functions** (`run/include/entity/store/index.h`):

- `entity_store_start`: an empty store, from an arena.
- `entity_store_spawn`, `entity_store_remove`: a new body, and the end of one.
- `entity_store_index_of`: where a body is now, from its handle, or that it is dead.
- `entity_handle_slot`: what stays the same for a body's life, for a system with an array of its own per body.
- `entity_store_box`: a body's box.
- `entity_store_wish_set`, `entity_store_jump`: what a mind asks of a body.
- `entity_store_tick`: one tick of every body.
- `ENTITY_STORE_SEX_NONE`, `_MALE`, `_FEMALE`: what `borrowed_sex` holds. Below.

**May be called by:** `run/src/entity/`, `run/src/entity/grid/`, `run/src/entity/animation/`, `run/src/entity/explosion/` and `projectile/`, `run/src/mob/` and what it has for a mob, `run/src/player/combat/`, and `run/src/game/` to draw them.
**Depends on:** `arena`, `physics`, `assert`, `world/map`, `world/column`, and the generated `tables/blocks`.

## What it does

**Arrays, not bodies.** A tick reads a few fields of everyone, so each field is an array, and the first `count` of each are in use. A removal moves the last body into the gap, so the arrays have no holes to step over. It also means an index is good only until the next removal, and a walk that removes goes from the end.

**A handle is what is kept.** `entity_handle_t` is 22 bits of generation over 10 bits of slot, and never 0, which is `ENTITY_HANDLE_NONE`. A slot's generation goes up when its body goes, and the free slots are a ring, so that the slot used longest ago is used next: it takes four thousand million spawns for an old handle to mean someone new. `entity_store_index_of` checks the handle against the body it finds, so a handle that was never given finds nothing either. A full store refuses, and counts it in `refused_count`: unlike a lost item, a mob that was not spawned has nobody to miss it.

**The same physics as a player.** Every move is `physics_move` and every check `physics_box_is_clear`, from `run/src/physics/`, on the `physics_t` the sim has. So a body stops at exactly what a player stops at, a column that is not loaded is a wall, and no speed goes through anything. What it adds is what `run/src/player/movement/` adds to a person, with the same published numbers:

- **Gravity** 0.08 a tick, kept at 0.98; **sideways** kept at 0.91 in the air and 0.546 on plain ground, 0.8918 on ice and 0.89999 on blue ice.
- **A wish** is in blocks a tick, the speed it comes to on plain ground. On a slippery floor it has less grip, by the cube of what the floor keeps, as a person has. Off the ground it pushes at a fixed part of that: a published zombie's 0.02 of air to its 0.23 of ground.
- **Steps** of 0.6, for what is walked into on the ground, if going up, along and down gets further than the wall let it.
- **Water and lava:** drag of 0.8 and 0.5, gravity of 0.02, and a jump as a rise of 0.04. Swimming into a bank with room above it is a hop out, of 0.3. Both break a fall.
- **Ladders and vines** in the cell of its feet: at most 0.15 a tick either way sideways and down, and up at 0.2 if it wants to go up or walks into something.
- **A fall** adds up as a person's does, in `fall_distance`, and its total is in `landed_fall` for the one tick it lands, for `run/src/mob/combat/`.

Why a copy of those lines and not `run/src/player/movement/`'s: that takes one body's struct and a person's keys, it has crouching and sprinting that no mob has, and a tick of 1024 of them should not build 1024 of its structs. What they have in common is `run/src/physics/`, which they share.

**Waiting.** A body with a corner over a column that is not loaded stays exactly as it was, velocity, age and all, with `ENTITY_STORE_FLAG_WAITING` set, until the column comes. So the view moving away cannot lose one, and it never falls through what is not there yet.

`ENTITY_STORE_FLAG_JUMP` lasts until the next tick it moves, and is used up then whether it could jump or not.

Everything it reads and writes is plain numbers, so the same world and the same calls give the same bytes.

**A body's sex** (plan 005 phase 4) is `borrowed_sex`, one of `ENTITY_STORE_SEX_NONE`, `_MALE` and `_FEMALE`, given by `entity_store_spawn_t.sex` and kept for the body's life, moving with it when another goes. Only a mob with `male:` and `female:` parts in `config/mobs/` is either, and `run/src/mob/spawn/` chooses which; anything else is none. The numbers are `tables/mobs/`'s `MOB_SEX_`, which `run/src/mob/spawn/` and `run/src/entity/animation/` assert. `run/src/save/state/` saves it in a section of its own, so a world saved before it opens with every body none.
