# compile/config/render

Tuning for the renderer. The fields of a key are in `../README.md`.

| File | Defines | What it tunes | Read by |
|---|---|---|---|
| `lod/index.yaml` | `MC_TUNE_RENDER_LOD_*` | The far field's levels: how wide each is, how much past what rays use is kept built, which levels grow trees, the pixel error and the bands (plan 009 phase 04), and how far the game holds blocks | `run/include/render/lod/`, `run/include/gpu_bridge/lod/` |
| `traverse/index.yaml` | `MC_TUNE_RENDER_TRAVERSE_*` | The coarse pass: the stretches of ray it tries to clear at once, and the pads that keep it from skipping a block | `cuda/src/traverse/` |
| `shade/index.yaml` | `MC_TUNE_RENDER_SHADE_*` | Surfaces a ray goes through by holes, AO's turns, rays, samples and reach, god rays' steps, how far a ray looks through water | `run/include/gpu_bridge/shade/` |
| `temporal/index.yaml` | `MC_TUNE_RENDER_TEMPORAL_*` | TAA: how much a frame adds, how far history may be from its neighbours, and when the camera counts as still | `run/include/gpu_bridge/post/` |
| `resolution/index.yaml` | `MC_TUNE_RENDER_RESOLUTION_*` | Plan 008 phase 05: the ladder's rungs under the render scale, and the controller that picks one for each picture: the share of the slot a rung must fit, the turn speed that steps down before a late picture, the pictures to step up and to go back to the top when still | `run/include/game/pace/`, `run/src/settings/persist/` |
| `budget/index.yaml` | `MC_TUNE_RENDER_BUDGET_*` | What a frame may send to the card, and how many far-field columns are built at once | `run/include/render/upload/`, `run/include/render/lod/` |

Each header is included only where it is used, so a change to one file rebuilds only what reads it. `shade` and
`temporal` are read by `gpu_bridge` headers that most of the renderer includes, so a change there rebuilds most of
`cuda/`; `traverse` rebuilds one CUDA file.
