# Iris dynamic VAO fast path on Gfx9 (TODO item 21)

`iris_screen.c` sets `allow_dynamic_vao_fastpath = devinfo->ver >= 11`, with a comment that generated VERTEX_BUFFER_STATEs being cached separately makes the fast path harmful on Gfx8/9. A measurement-only build added `IRIS_GFX9_DYNAMIC_VAO_FASTPATH=true` (release Iris, round-2 tree). glmark2 2023.01 ran off-screen with `--frame-end finish`, for 5 alternating rounds (`timing.txt`).

| Scene | Off (median fps) | On (median fps) | Change |
| --- | --- | --- | --- |
| `build:use-vbo=false` | 3489 | 3535 | +1.3% (noise; ranges overlap) |
| `build:use-vbo=true` | 6482 | 5961 | **−8.0%** (lower in 4 of 5 rounds) |
| `buffer:update-method=map:update-fraction=0.5` | 855 | 864 | +1.1% (noise) |
| `buffer:update-method=subdata:update-fraction=0.5` | 790 | 807 | +2.2% (noise) |

With the fast path on, `--validate` passed for build (VBO and client arrays), buffer, texture and shading.

**Decision: keep it disabled on Gfx9.** It is correct but gives no measurable gain, and it costs about 8% in the VBO scene, which matches the existing comment. The measurement toggle was never added to the project tree.
