# cuda/src/post

**Purpose:** from shade's pixels to what goes on the screen: the sky where nothing was hit, distance fog, and the tone map to XRGB8888.
**Tier:** a CUDA area. It holds no memory. Device blocks come from its caller, and are checked against what is asked of them.
**Owns:** nothing.
**Lifetime:** each call starts a kernel and returns.
**Threads:** the `gpu` thread, or the one thread of a run with no threads.
**Public functions** (`run/include/gpu_bridge/post/index.h`, for C and CUDA):

- `post_frame`: the kernel.
- `post_xrgb_bytes`: what its target needs.
- `post_sky`, `post_fog`, `post_tone_map`, `post_xrgb`: its arithmetic, inline on host and device, so that a test on the host runs the same lines.

**May be called by:** `render`, `cuda/src/frame/`.
**Depends on:** `gpu_bridge/device_memory`, `gpu_bridge/status`, `gpu_bridge/traverse`, `gpu_bridge/shade` for the form of its input, and `cuda/include/traverse/` for a pixel's direction.

## The tone map

A straight line up to 0.8, and a smooth roll towards 1 past it, then the sRGB curve. Up to 0.8 in linear light a texel under full brightness comes out as the byte it went in as, give or take rounding, which a test checks: nothing here may wash out the pack's colours. The roll is for later, when stored light and block light can add up past 1. There is no dither, because one script must give one picture, byte for byte.

## The fog

From nothing at 70 % of `fog_end` to all sky at `fog_end`, which is a chunk short of what is loaded, so that the edge of the loaded world melts into the sky. It is here because a frame without it shows that edge as a wall. Stage 09.x owes a better one, with colour from the sun.
