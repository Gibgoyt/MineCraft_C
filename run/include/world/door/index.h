/* See run/src/world/door/README.md. */
#ifndef MC_RUN_WORLD_DOOR_INDEX_H
#define MC_RUN_WORLD_DOOR_INDEX_H

#include "tables/blocks/index.h"

#include <stdint.h>

/* Whether this block id is a door: shape: door in its file. */
bool door_is(uint32_t id);

/* Whether the right button opens and shuts it. An iron door is not, as nothing else can open it yet: there is no redstone. */
bool door_opens_by_hand(uint32_t id);

/* Whether this voxel is half a door. If so, the step up or down to its other half, and that half as it must be there: the same door,
 * facing and open or shut alike, the other half. */
bool door_half_other(uint16_t voxel, int32_t *out_step_z, uint16_t *out_other);

/* What a door's facing is to be for its hinge, from the facing the player gives it. A door's hinge is at its left end as it faces, so a
 * door hinged on the right faces the other way, with its plate on the near side of its cell. What is beside the lower half decides,
 * as Minecraft's does: another door at one side hinges this one on the other, making a pair; otherwise the side with more of a wall
 * beside it, lower and upper, takes the hinge; and with neither, the left. left_walls and right_walls are 0 to 2. */
uint32_t door_hinged_facing(uint32_t facing, bool door_left, bool door_right, uint32_t left_walls,
                            uint32_t right_walls);

#endif
