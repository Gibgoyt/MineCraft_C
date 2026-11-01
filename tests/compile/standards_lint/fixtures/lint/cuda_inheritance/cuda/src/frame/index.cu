typedef struct frame_t : base_t {
    uint32_t value;
} frame_t;
static_assert(sizeof(frame_t) == 4);
