#!/usr/bin/env python3
"""Compare sampled-texel dumps from astc-host-copy ASTC_DUMP runs.

usage: compare-dumps.py REFERENCE_DIR CANDIDATE_DIR [TOLERANCE]

Prints, per dump, the number of RGBA channels differing by more than the
tolerance (default 1.5/255, the RGBA8 quantisation of the emulated plane).
Exit 1 if any dump differs.
"""
import array
import pathlib
import sys

ref_dir, cand_dir = map(pathlib.Path, sys.argv[1:3])
tol = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5 / 255
failed = False
for ref in sorted(ref_dir.glob("*.bin")):
    cand = cand_dir / ref.name
    a, b = array.array("f"), array.array("f")
    a.frombytes(ref.read_bytes())
    b.frombytes(cand.read_bytes())
    if len(a) != len(b):
        print(f"{ref.stem:28} size mismatch")
        failed = True
        continue
    diffs = [abs(x - y) for x, y in zip(a, b)]
    bad = sum(1 for d in diffs if not d <= tol)
    worst = max(diffs)
    print(f"{ref.stem:28} {bad:6}/{len(a)} channels over tolerance, worst {worst:.6f}")
    failed |= bad > 0
sys.exit(1 if failed else 0)
