typedef struct [[gnu::packed]] world_t {
    uint8_t a;
} world_t;
static_assert(sizeof(world_t) == 1);
