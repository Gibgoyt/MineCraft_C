# run/src/ui/compose

**Purpose:** puts pixels on a frame, every one of them through `blend_over_batch` in `asm/src/blend/`: the rect and image types, clipping, one colour, an image stretched by nearest pixel, and an image in nine parts.
**Tier:** Arena. `ui_image_load` allocates from the arena it is given. Everything else allocates nothing.
**Owns:** nothing.
**Lifetime:** A `ui_image_t` lasts as long as the arena it came from. A `ui_canvas_t` lasts one frame.
**Threads:** `main`. A canvas's row scratch is one buffer, so one canvas belongs to one thread.
**Public functions** (`run/include/ui/compose/index.h`):

- `ui_canvas_start`, `ui_canvas_clip_set`: a canvas over pixels; what it may touch.
- `ui_rect_intersect`, `ui_rect_contains`, `ui_rect_center`: rects, in 64 bits inside, so that one near the ends of `int32_t` is still right.
- `ui_image_load`: a texture's first frame at full size, as 0xAARRGGBB, once, not per frame.
- `ui_compose_span`: the one call of the kernel, for source pixels already clipped.
- `ui_compose_fill`, `ui_compose_image`, `ui_compose_image_whole`, `ui_compose_nine`: the rest, clipped.

**May be called by:** `ui` and its parts. `font` and `widget` are built on it, as its siblings, and this README is what allows that (`plans/REPO_STRUCTURE.md` section 11).
**Depends on:** `arena`, `assert`, `log`, `texture`, `simd/blend`.

## Why batches

A kernel call has a cost, so nothing here calls it per pixel. A fill or an image makes one call per row of the rect. A row drawn several times over, as when an image is scaled up, is built once in the row scratch and blended as many times as it is tall. An image at its own width needs no scratch at all: the kernel reads the image directly.

`blend_premul_batch` is not used. It is for building up a layer first, and nothing here does: the textures hold straight alpha, the frame takes them directly, and a layer the size of the frame would be cleared and blended every frame for nothing.

Tested by ctest `run_ui_font`, against a model that uses `simd_reference_blend_over` a pixel at a time, with guard pixels all round to catch a write outside the canvas.
