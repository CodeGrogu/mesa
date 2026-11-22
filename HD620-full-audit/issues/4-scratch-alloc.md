## Summary

`anv_scratch_pool_alloc()` returns NULL when the scratch BO cannot be allocated, which the source marks with a TODO. ANV ignored that return value. On Gfx9–12 the NULL BO became `ScratchSpaceBasePointer` 0, so `vkCreate*Pipelines` returned `VK_SUCCESS` and the shader's scratch accesses went to address 0. On Gfx12.5+, `anv_scratch_pool_get_surf()` dereferences the NULL BO, and `anv_shader_get_scratch_surf()` relocates it.

## Reproducer ([`scratch-oom`](https://github.com/CodeGrogu/mesa/tree/hd620-audit/HD620-full-audit/scratch-oom))

The `gemfail.so` `LD_PRELOAD` shim fails i915 GEM creates at or above a size limit, here the 42 MiB scratch BO of `spill.comp`.

| Driver | Scratch BO | `vkCreateComputePipelines` |
| --- | --- | --- |
| main | fails | **`VK_SUCCESS`** (scratch at address 0) |
| `hd620-round2` | fails | `VK_ERROR_OUT_OF_DEVICE_MEMORY` |
| `hd620-round2` | succeeds | `VK_SUCCESS`, 4,096/4,096 outputs correct |

## Fix

Commit "anv: report scratch BO allocation failure at shader creation" reserves the scratch BOs in `anv_shader_create()`. The BOs are cached for the device's lifetime, so lookups during state emission cannot fail afterwards. Command-time and internal simple-shader paths set a batch error instead of using a NULL BO.
