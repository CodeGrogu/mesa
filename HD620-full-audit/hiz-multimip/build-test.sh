#!/usr/bin/env bash
# Compile the HiZ test inside the existing hd620-mesa container (no host toolchain needed).
set -eu
cd "$(dirname "$0")"
mkdir -p tools
tar cf - vulkan-hiz-mips.c depth_vert.h fetch_comp.h | docker exec -i hd620-mesa sh -c \
  'set -e; d=$(mktemp -d); trap "rm -rf $d" EXIT; tar xf - -C $d; gcc -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers -I/src/include -o $d/t $d/vulkan-hiz-mips.c -l:libvulkan.so.1 -lm; cat $d/t' > tools/vulkan-hiz-mips
chmod +x tools/vulkan-hiz-mips
sha256sum tools/vulkan-hiz-mips vulkan-hiz-mips.c depth.vert fetch.comp depth_vert.h fetch_comp.h
