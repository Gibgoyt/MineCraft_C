# run/src/entity/animation

**Purpose:** what a body looks like as boxes: the parts of each kind of body, how walking, looking, attacking, aiming and swelling move them, and where they all are at a moment.
**Tier:** Arena by its depth. It allocates nothing: rigs are constants over the generated `tables/mobs/`, what moves is in `entity_store_t`, and a pose goes where it is told.
**Owns:** nothing.
**Lifetime:** Process for the rigs; what moves is the store's.
**Threads:** `entity_animation_tick` and `entity_animation_swing` on `sim`. A rig may be read by anyone, and `entity_animation_pose` takes what it is given, so it can run anywhere.
**Public functions** (`run/include/entity/animation/index.h`):

- `entity_animation_rig`: a kind of body's parts, for the three named rigs.
- `entity_animation_rig_of`, `entity_animation_sex_rig_of`, `entity_animation_scale_of`: any mob's own parts, a male's or a female's besides them, and how much bigger that one is.
- `entity_animation_tick`: one tick of every body's stride, and of its attack.
- `entity_animation_swing`: an attack starts.
- `entity_animation_state_read`: how one body is moving now.
- `entity_animation_pose`, `entity_animation_pose_scaled`: where a rig's parts are, for a state at a place, at a size.
- `entity_animation_body_pose`: every box of one body in the store, its kind's and its sex's, at its sex's size: what `run/src/game/` draws.

**May be called by:** `run/src/entity/`, which ticks it, `run/src/mob/zombie/`, and `run/src/game/`, which draws a body.
**Depends on:** `entity/store`, libm.

## What it does

**A part** is one box in a model's pixels, 16 to a block: its size, the pivot it turns about, measured from the middle of the soles of the feet, where the box's middle is from that pivot, its pitch at rest, how far a full stride swings it, and the corner of its box-mapped unwrap on the texture. Axes are forward, left and up, with forward where the yaw points, as `render_entities_item_t`'s are.

**The rigs** are the `parts` of `config/mobs/<mob>/index.yaml`, and `entity_animation_part_t` is `tables/mobs/`'s `mob_part_t`:

| Rig | Parts | Texture |
|---|---|---|
| Zombie | Head 8 by 8 by 8, body 4 by 8 by 12, arms 4 by 4 by 12 held straight out, and legs 4 by 4 by 12 | `textures/mobs/zombie/`, 64 by 32 |
| Skeleton | The same head and body, arms and legs of 2 by 2 by 12, and arms that swing | `textures/mobs/skeleton/`, 64 by 32 |
| Creeper | The same head and body, lower, and four feet of 4 by 4 by 6, swinging in pairs across it | `textures/mobs/creeper/`, 64 by 32 |

**The stride** is published. Each tick the pace wanted is four times its speed that tick, at most 1, and `limb_amount` goes 0.4 of the way there; `limb_phase` goes on by `limb_amount`, and starts over each whole stride, so that it keeps its exactness all night. A leg's pitch is `walk_swing * cos(phase * 0.6662) * limb_amount`, which is 1.4 at the most for a leg and 1.0 for an arm that swings, with the other side's sign the other way. A body waiting for a column holds its stride, though its attack still ends.

**What else moves one:**

- `head_yaw` and `head_pitch` for the head.
- `swing`, an attack of 6 ticks, lifts an arm by 1.2 radians at its height.
- `aim`, from 0 to 1, takes an arm from whatever it was doing to straight out.
- `swell`, from 0 to 1, grows every part by up to a fifth about its own middle, for a creeper about to burst.

A pose's `half` and `pitch` are the part as it really is. **`reach`** is the box that holds all of it if it is turned by yaw alone, for whoever wants a bound without the pitch; what is drawn leans by `pitch` itself, as `cuda/src/entity_rays/` can: a box with no pitch of its own. For an arm held straight out, that is exactly the arm.

`run/src/game/` draws them: `game_mobs_publish` gives each part's middle, `half`, `yaw` and `pitch` to `render_entities_add_mob_part`, with its share of the body's skin, every tick.

**A male and a female** (plan 005 phase 4). A mob with `male:` and `female:` parts in `config/mobs/` gives each its own boxes besides the mob's, such as horns, tusks, an udder or a comb, and a `scale`. `entity_animation_body_pose` poses the mob's own parts and then its sex's, every size and place from the feet times that scale, into one list of at most `ENTITY_ANIMATION_PARTS_MAX`, 16, which the tables keep to; a body that is neither has its own parts at 1. The texture is the mob's, so a sex's boxes are unwrapped on it too. `game_mobs_publish` draws any mob this way, so a new mob in `config/mobs/` needs no rig here.
