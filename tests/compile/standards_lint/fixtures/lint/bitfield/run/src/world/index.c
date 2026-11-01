typedef struct world_t {
    uint32_t a : 3;
    uint32_t b : 29;
} world_t;
static_assert(sizeof(world_t) == 4);
