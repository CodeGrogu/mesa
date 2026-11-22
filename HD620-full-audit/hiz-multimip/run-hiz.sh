#!/usr/bin/env bash
# Gen9 multi-mip HiZ validation runner.
#   run-hiz.sh correctness   loader proof, sizes, pixel tests, checker self-test, batch decode
#   run-hiz.sh timing        GPU timings (run only after correctness passes)
# Drivers: candidate (multi-mip HiZ gate) and control (approved patch8 build),
# each also under INTEL_DEBUG=nohiz.
set -euo pipefail
cd "$(dirname "$0")"
EVID=${HIZ_EVIDENCE_DIR:-../evidence/hiz-multimip}
STATE=$HOME/.local/state/catchingup
CAND=$STATE/hd620-hiz-multimip/icd/candidate/icd.json
CTRL=$STATE/hd620-anv-query-verification/patch8/icd/patch8-all/icd.json
mkdir -p "$EVID"
unset VK_ICD_FILENAMES VK_ADD_DRIVER_FILES ANV_DEBUG INTEL_DEBUG VK_LOADER_DEBUG

run() { # name icd intel_debug args...
    local name=$1 icd=$2 dbg=$3; shift 3
    if [[ -n $dbg ]]; then
        env INTEL_DEBUG="$dbg" VK_DRIVER_FILES="$icd" tools/vulkan-hiz-mips "$@"
    else
        env VK_DRIVER_FILES="$icd" tools/vulkan-hiz-mips "$@"
    fi
}

declare -A ICD=([candidate]=$CAND [control]=$CTRL)

if [[ ${1:-correctness} == correctness ]]; then
    {
        sha256sum tools/vulkan-hiz-mips "$(dirname "$CAND")/libvulkan_intel.so" "$(dirname "$CTRL")/libvulkan_intel.so"
        cat "$CAND" "$CTRL"
    } > "$EVID/identity.txt"
    for drv in candidate control; do
        for dbg in "" nohiz; do
            tag=$drv-${dbg:-default}
            VK_LOADER_DEBUG=driver run "$tag" "${ICD[$drv]}" "$dbg" sizes > "$EVID/sizes-$tag.log" 2> "$EVID/loader-$tag.log"
            grep -q "Using \"Intel(R) HD Graphics 620 (KBL GT2)\" with driver: \"$(dirname "${ICD[$drv]}")/libvulkan_intel.so\"" \
                "$EVID/loader-$tag.log" || { echo "loader did not pick $drv"; exit 1; }
            log=$EVID/correct-$tag.log
            rc=0
            run "$tag" "${ICD[$drv]}" "$dbg" correct > "$log" 2>&1 || rc=$?
            echo "rc=$rc" >> "$log"
            [[ $rc == 0 ]] && grep -Fxq 'SUMMARY 16/16 pixel cases passed' "$log" || exit 1
        done
    done
    log=$EVID/checker-selftest-candidate.log
    rc=0
    HIZ_TEST_PERTURB=1 run perturb "$CAND" "" correct aligned-704x288x4 > "$log" 2>&1 || rc=$?
    echo "rc=$rc (expected 2)" >> "$log"
    [[ $rc == 2 ]] && grep -Fxq 'SUMMARY 0/1 pixel cases passed' "$log" || exit 1
    tmp=$(mktemp)
    trap 'rm -f -- "$tmp"' EXIT
    for drv in candidate control; do
        for c in aligned-704x288x4 oddlod0-17x9x2 aligned-256x128x6 unaligned-696x288x4 fullchain-64x32x7 resolve-704x288x4 resolve-17x9x2; do
            run bat "${ICD[$drv]}" bat correct "$c" > "$tmp" 2>&1
            grep -Fxq 'SUMMARY 1/1 pixel cases passed' "$tmp"
            { grep -E '^(PASS|FAIL|SUMMARY|driver library)' "$tmp"; python3 -I bat-summary.py "$tmp"; } \
                > "$EVID/bat-$drv-$c.txt"
        done
    done
    python3 -I - "$EVID" <<'EOF' | tee "$EVID/sizes-verdict.txt"
import sys, re, pathlib
e = pathlib.Path(sys.argv[1])
def load(t):
    d = {}
    for l in (e / f"sizes-{t}.log").read_text().splitlines():
        m = re.match(r"size (\S+) (\S+) (\d+)", l)
        if m: d[m.group(1)] = (m.group(2), int(m.group(3)))
    return d
cd, cn, kd, kn = (load(t) for t in ("candidate-default", "candidate-nohiz", "control-default", "control-nohiz"))
if not cd or cd.keys() != cn.keys() or cd.keys() != kd.keys() or cd.keys() != kn.keys():
    sys.exit("Size runs did not report the same nonempty case set")
ok = True
for name, (scope, size) in cd.items():
    hiz = size > cn[name][1]
    ctrl_hiz = kd[name][1] > kn[name][1]
    multi = not name.startswith("single")
    want = scope == "in-scope"
    good = hiz == want and (ctrl_hiz == (not multi and want)) and cn[name][1] == kn[name][1]
    ok &= good
    print(f"{'ok ' if good else 'BAD'} {name:24} {scope:12} candidate {size:>9} nohiz {cn[name][1]:>9} "
          f"-> HiZ {'yes' if hiz else 'no '} | control {kd[name][1]:>9} nohiz {kn[name][1]:>9} "
          f"-> HiZ {'yes' if ctrl_hiz else 'no'}")
print("SIZES VERDICT", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
EOF
elif [[ $1 == timing ]]; then
    for tag in candidate-default candidate-nohiz control-default; do
        drv=${tag%%-*}; dbg=${tag#*-}; [[ $dbg == default ]] && dbg=
        run "$tag" "${ICD[$drv]}" "$dbg" timing > "$EVID/timing-$tag.log" 2>&1
    done
else
    echo "Usage: $0 [correctness|timing]" >&2
    exit 2
fi
