#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
docker exec -i hd620-mesa sh -c 'set -e; bin=$(mktemp); trap "rm -f $bin" EXIT; gcc -O2 -Wall -Wextra -I/src/include -x c -o "$bin" - -l:libvulkan.so.1; cat "$bin"' < vulkan-depth.c > tools/vulkan-depth
chmod +x tools/vulkan-depth
unset VK_ICD_FILENAMES VK_ADD_DRIVER_FILES ANV_DEBUG INTEL_DEBUG
export VK_LOADER_DEBUG=driver
for driver in installed original patched; do
    if [[ $driver == installed ]]; then
        icd=/usr/share/vulkan/icd.d/intel_icd.json
    else
        icd=$(python3 -c 'import json, sys; print(json.load(open("../HD620-benchmark/provenance.json"))[sys.argv[1]]["manifest"])' "$driver")
    fi
    VK_DRIVER_FILES="$icd" tools/vulkan-depth > "evidence/depth-$driver-default.txt" 2>&1
    INTEL_DEBUG=nohiz VK_DRIVER_FILES="$icd" tools/vulkan-depth > "evidence/depth-$driver-nohiz.txt" 2>&1
done
