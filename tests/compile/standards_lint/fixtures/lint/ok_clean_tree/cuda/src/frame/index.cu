#include "frame/index.hu"
/* In CUDA an unsuffixed literal is the right float64_t, and F is float32_t. */
static float64_t frame_half = 0.5;
static float32_t frame_quarter = 0.25F;
extern "C" void frame_run(void) {
    frame_step(frame_half, frame_quarter);
}
