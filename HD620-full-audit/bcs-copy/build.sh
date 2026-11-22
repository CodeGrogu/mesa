#!/usr/bin/env bash
# Compile both copy fixtures inside the existing hd620-mesa container. Sources are streamed
# in over stdin and binaries streamed back; nothing in the container or host is installed.
# Compiling does not open a GPU device.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p bin
CFLAGS='-O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function'
tar cf - bcs-common.h i915-bcs-copy.c vulkan-render-copy.c |
    docker exec -i hd620-mesa sh -euc "
        d=\$(mktemp -d); trap 'rm -rf \$d' EXIT; cd \$d; tar xf - >&2
        gcc $CFLAGS -ffile-prefix-map=\$d=. -I/src/include/drm-uapi -o i915-bcs-copy i915-bcs-copy.c -lm >&2
        gcc $CFLAGS -ffile-prefix-map=\$d=. -I/src/include -o vulkan-render-copy vulkan-render-copy.c -lm -l:libvulkan.so.1 >&2
        tar cf - i915-bcs-copy vulkan-render-copy" | tar xf - -C bin
sha256sum bin/i915-bcs-copy bin/vulkan-render-copy bcs-common.h i915-bcs-copy.c vulkan-render-copy.c
