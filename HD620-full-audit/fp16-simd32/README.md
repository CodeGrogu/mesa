# Gfx9 SIMD32 FP16 three-source lane-mask check (Mesa #16528)

`lanemask.comp` runs two FP16 `fma` operations inside a branch with a memory write, so NIR keeps the branch and the multiply-adds execute under a divergent execution mask. `lanemask.c` dispatches it at required subgroup sizes 8, 16 and 32 with four mask patterns and checks every one of 65,536 lanes exactly. Inputs are small integers and halves, so every expected value is exactly representable in FP16.

```bash
../tools/root/usr/bin/glslangValidator --target-env vulkan1.3 -V lanemask.comp -o lanemask.spv
docker exec -i hd620-mesa sh -c 'set -e; bin=$(mktemp); trap "rm -f $bin" EXIT; gcc -O2 -Wall -Wextra -I/src/include -x c -o "$bin" - -l:libvulkan.so.1 -lm; cat "$bin"' < lanemask.c > lanemask
VK_DRIVER_FILES=<icd.json> ./lanemask lanemask.spv
```

## Results on HD 620 (KBL GT2)

| Driver | SIMD8 / SIMD16 | SIMD32 | Log |
| --- | --- | --- | --- |
| Installed Mesa 26.0.8 | 0 mismatches | 31,931 wrong values, all in lanes 16–31 | `installed.txt` |
| Checkout `0e9e108` + eight ANV patches | 0 | 38,349 wrong values | `checkout-patch8.txt` |
| Checkout + upstream MR 45047 (`313753a5…`) | 0 | **0** (3 repeats) | `mr45047.txt` |

Pattern 2 activates only lane 21 of each 32. The unpatched drivers left lane 21 unwritten and wrote lane 29 (21 + 8), the L − 8 behaviour described upstream.

With `MESA_SHADER_CACHE_DISABLE=true INTEL_DEBUG=cs`, the control compiles the SIMD32 shader to `mad (32) … :hf {Align16}` (`asm-control.txt`). The MR build emits `mad (16)` and `mad (16|M16)` pairs (`asm-mr45047.txt`).

The MR applied to the checkout with `patch -p1` in a container copy (`/build/fp16-src`). Unit tests: `intel_compiler_brw_tests` 410/410 and `intel_compiler_gen_tests` 150/150 passed, including the MR's `gfx9_simd32_hf_mad` and `simd32_mad_*` cases.

The companion hang (#16526, SIMD32 `mad` with a conditional modifier) was not reproduced because it resets the GPU under the running desktop.
