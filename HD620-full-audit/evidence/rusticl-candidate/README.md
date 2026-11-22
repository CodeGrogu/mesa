# HD 620 Rusticl timer verification

Meson configured the Iris candidate with Rusticl enabled. The build and hardware runs are pending. Run them serially with the HiZ validation to avoid competing for CPU and GPU time.

The `hd620-mesa` build container has rustc and Cargo 1.93.1, bindgen 0.72.1, and the OpenCL ICD loader. Meson needs a writable source copy to unpack Rust dependencies. The original `/src` mount is read-only, so configuration uses the container copy recorded in `source-path.txt`. `source-diff.sha256` identifies the local Mesa changes copied for this run. See `setup-snapshot.log` for the successful configuration.

```bash
docker exec hd620-mesa ninja -j1 -C /build/audit-rusticl \
  src/gallium/targets/rusticl/libRusticlOpenCL.so.1.0.0
docker exec -i hd620-mesa sh -eu -c '
  library=/build/audit-rusticl/src/gallium/targets/rusticl/libRusticlOpenCL.so.1.0.0
  vendors=$(mktemp -d)
  trap '\''rm -rf "$vendors"'\'' EXIT
  printf "%s\n" "$library" > "$vendors/rusticl.icd"
  sha256sum "$library"
  OCL_ICD_VENDORS="$vendors" RUSTICL_ENABLE=iris python3 -c '\''
import sys
library = sys.argv[1]
sys.argv = ["opencl-smoke.py", "--require-valid-profiling"]
paths = {line.split()[-1] for line in open("/proc/self/maps")
         if "libRusticlOpenCL" in line}
assert not paths, paths
exec(compile(sys.stdin.read(), "opencl-smoke.py", "exec"))
paths = {line.split()[-1] for line in open("/proc/self/maps")
         if "libRusticlOpenCL" in line}
assert paths == {library}, paths
print("Rusticl library", *paths)
'\'' "$library"
' < HD620-full-audit/opencl-smoke.py
```

The probe checks device identity, kernel results, event timestamps, wall time, and i915 engine activity. The profiling gate requires event duration to grow with workload size and to cover a substantial part of the measured GPU activity. Run the same probe on the staged baseline as a negative control. The mode is specific to this HD 620 workload.

## Result (2026-11-19)

The `hd620-mesa` container had only `libclc-21-dev`, so the candidate first failed to load libclc. The staged `spirv-mesa3d-.spv` and `spirv64-mesa3d-.spv` from `tools/root/usr/lib/clc` were copied into the container's `/usr/lib/clc`.

| Build | 256-round kernel event | i915 render busy | Gate |
| --- | --- | --- | --- |
| Staged baseline | 250 ns | 11.10 ms | Fails as expected (`baseline-profiling.log`) |
| Timer candidate (`1a7dc828…`) | 11.56 ms | 11.18 ms | **PASS** (`candidate-profiling.log`) |

The Iris timer-ordering fix also corrects Rusticl event profiling on HD 620.
