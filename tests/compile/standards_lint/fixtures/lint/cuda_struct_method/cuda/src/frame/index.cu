typedef struct frame_t {
    uint32_t get(void) {
        return 1u;
    }
    uint32_t value;
} frame_t;
static_assert(sizeof(frame_t) == 4);
