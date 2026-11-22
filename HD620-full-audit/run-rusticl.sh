#!/usr/bin/env bash
# Use the staged OpenCL ICD and libclc without modifying the host's ICDs.
set -eu
cd "$(dirname "$0")"
docker run --rm --device=/dev/dri/renderD128 -v "$PWD:/audit:ro" \
  -v "$PWD/tools/root/usr/lib/clc:/usr/lib/clc:ro" \
  -v "$PWD/tools/root/usr/lib/llvm-21:/usr/lib/llvm-21:ro" \
  -v /usr/lib/x86_64-linux-gnu:/host-libs:ro \
  -v /usr/bin/python3.14:/usr/bin/python3.14:ro -v /usr/lib/python3.14:/usr/lib/python3.14:ro \
  -e LD_LIBRARY_PATH=/audit/tools/root/usr/lib/x86_64-linux-gnu:/host-libs \
  -e OCL_ICD_VENDORS=/audit/tools/icd -e RUSTICL_ENABLE=iris \
  ubuntu:26.04 /host-libs/ld-linux-x86-64.so.2 "$@"
