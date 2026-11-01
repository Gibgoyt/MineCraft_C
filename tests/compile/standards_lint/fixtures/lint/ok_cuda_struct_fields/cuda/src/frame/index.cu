typedef struct alignas(8) frame_t {
    void (*callback)(void);
    uint8_t bytes[sizeof(uint64_t)];
} frame_t;
static_assert(sizeof(frame_t) == 16);
