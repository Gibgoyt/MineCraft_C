typedef struct frame_t {
    frame_t(void);
    uint32_t value;
} frame_t;
static_assert(sizeof(frame_t) == 4);
