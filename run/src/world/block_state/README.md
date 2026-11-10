# run/src/world/block_state

**Purpose:** what each block may hold of the state beside its id, from the generated table: which blocks turn, which have halves, which open. The bits themselves are `run/include/types/block_state/`'s; this is the part that needs `block_defs`.
**Tier:** Arena (depth 2), and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. It reads only the generated tables.
**Public functions** (`run/include/world/block_state/index.h`):

- `block_state_allowed`: the state bits a block's file declares, as a mask of `BLOCK_STATE_BITS_MASK`.
- `block_state_turns`: whether the block has a facing (`facing: horizontal`).
- `block_state_clean`: a voxel with only the bits its block declares. What a save holds goes through this before it reaches the world, so a block that has stopped turning since it was saved is drawn as it is, and a hand-edited file cannot put a facing on dirt.
- `block_state_placed`: what putting a block down facing a way makes: that facing if it turns, and the id alone if not.

**May be called by:** anything with the block table: `save/world`, `player/interaction`, `world/bench`, `world/wall_torch`, and tests.
**Depends on:** the generated `tables/blocks/`, and `types/block_state`.

## Which blocks have a state

From `config/blocks/`, at the time of plan 005 phase 12:

| Field | Blocks |
|---|---|
| `facing: horizontal` | every stairs, `ladder`, `vine`, both halves of `woodworking_bench`, `wall_torch` |
| `half: upper_lower` | none yet: doors, in phase 2 |
| `open: true` | none yet: doors, in phase 2 |

The stoneworking bench and the smith table of phase 6 declare `facing: horizontal` too, and are placed facing the player with nothing more.
