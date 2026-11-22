#!/usr/bin/env bash
# GPU runs for the BCS copy fixture. FOR THE PARENT SCHEDULER ONLY: run with nothing else on
# the GPU, serially after other GPU work. Render node only, no root, no system changes.
# Evidence goes to ../evidence/bcs-copy/<UTC timestamp>/.
set -euo pipefail
cd "$(dirname "$0")"
out=../evidence/bcs-copy/$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$out"
icd=${VK_ICD:-/usr/share/vulkan/icd.d/intel_icd.json}
sha256sum bin/* bcs-common.h i915-bcs-copy.c vulkan-render-copy.c > "$out/sha256.txt"
{ uname -a; cat /proc/loadavg; } > "$out/host.txt"

# 1. Correctness gates. Any mismatch or wrong-engine accounting stops the run.
bin/i915-bcs-copy --verify > "$out/bcs-verify.jsonl" 2> "$out/bcs-verify.err"
VK_DRIVER_FILES=$icd bin/vulkan-render-copy --verify > "$out/vk-verify.jsonl" 2> "$out/vk-verify.err"

# 2. Timing. Same sizes, warm-up and sample counts on each route; order is recorded in headers.
sizes=${SIZES:-4K,64K,1M,8M,64M}
common=(--sizes "$sizes" --iters "${ITERS:-50}" --warmup "${WARMUP:-5}" --queue 16 --batch 16)
bin/i915-bcs-copy "${common[@]}" --modes cpu,latency,queued,batch --packet src \
    > "$out/bcs-src.jsonl" 2> "$out/bcs-src.err"
bin/i915-bcs-copy "${common[@]}" --modes latency,queued,batch --packet fast \
    > "$out/bcs-fast.jsonl" 2> "$out/bcs-fast.err"
VK_DRIVER_FILES=$icd bin/vulkan-render-copy "${common[@]}" \
    > "$out/vk-render.jsonl" 2> "$out/vk-render.err"
# 3. Reverse size order once to expose frequency or warm-up drift.
rev=$(tr ',' '\n' <<< "$sizes" | tac | paste -sd,)
bin/i915-bcs-copy --sizes "$rev" "${common[@]:2}" --modes cpu,latency,batch \
    > "$out/bcs-src-reverse.jsonl" 2> "$out/bcs-src-reverse.err"
python3 -I summarize.py "$out"/bcs-src.jsonl "$out"/bcs-fast.jsonl "$out"/vk-render.jsonl \
    "$out"/bcs-src-reverse.jsonl \
    > "$out/summary.md"
echo "$out"
