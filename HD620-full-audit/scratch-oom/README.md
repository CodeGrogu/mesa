# ANV scratch allocation failure handling

`spill.comp` has a large, dynamically indexed private array. ANV reports 16 KiB of scratch per thread for it, which makes a 44,040,192-byte scratch BO; every other BO in the run is 2 MiB or smaller. `gemfail.so` is an `LD_PRELOAD` shim that fails i915 `GEM_CREATE`/`GEM_CREATE_EXT` at or above `GEMFAIL_MIN_BYTES` with `ENOMEM`. `scratch-oom` creates the pipeline and reports the result. It dispatches only with `SCRATCH_DISPATCH=1`, because an unfixed driver would point scratch at address 0.

| Driver | Allocation | `vkCreateComputePipelines` | Log |
| --- | --- | --- | --- |
| control (checkout + eight patches) | normal | `VK_SUCCESS` | `control.txt` |
| control | scratch BO fails (injected) | **`VK_SUCCESS`**, with scratch programmed at address 0 | `control.txt` |
| fixed `31ced66e…` | normal | `VK_SUCCESS` | `fixed.txt` |
| fixed | scratch BO fails (injected) | **`VK_ERROR_OUT_OF_DEVICE_MEMORY`** | `fixed.txt` |

With `SCRATCH_DISPATCH=1`, both drivers computed all 4,096 outputs correctly (`dispatch.txt`), so the success path is unchanged.

The fix reserves the scratch BOs in `anv_shader_create()` through the new `anv_scratch_pool_reserve()`. Scratch BOs are cached for the device's lifetime, so lookups during state emission cannot fail afterwards. Command-time lookups on Gfx12.5+ and internal simple shaders mark the batch as failed instead of using a NULL BO. Patch: [`../patches/anv-scratch-alloc-errors.patch`](../patches/anv-scratch-alloc-errors.patch). The Gfx12.5+ surface paths were not hardware-tested here.
