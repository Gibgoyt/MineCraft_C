typedef struct world_t {
    uint8_t a;
} __attribute__((packed)) world_t;
static_assert(sizeof(world_t) == 1);
