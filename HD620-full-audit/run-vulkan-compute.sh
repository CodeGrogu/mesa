#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
tools/root/usr/bin/glslangValidator -V vector.comp -o tools/vector.spv
tools/root/usr/bin/spirv-val tools/vector.spv
docker exec -i hd620-mesa sh -c 'set -e; bin=$(mktemp); trap "rm -f $bin" EXIT; gcc -O2 -Wall -Wextra -I/src/include -x c -o "$bin" - -l:libvulkan.so.1; cat "$bin"' < vulkan-compute.c > tools/vulkan-compute
chmod +x tools/vulkan-compute
unset VK_ICD_FILENAMES VK_ADD_DRIVER_FILES ANV_DEBUG
for driver in installed original patched; do
    if [[ $driver == installed ]]; then
        icd=/usr/share/vulkan/icd.d/intel_icd.json
    else
        icd=$(python3 -c 'import json, sys; print(json.load(open("../HD620-benchmark/provenance.json"))[sys.argv[1]]["manifest"])' "$driver")
    fi
    VK_DRIVER_FILES="$icd" VK_LOADER_DEBUG=driver tools/vulkan-compute tools/vector.spv \
        > "evidence/compute-$driver.txt" 2>&1
done
