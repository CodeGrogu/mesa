#!/usr/bin/env python3
"""Summarize HiZ state from an INTEL_DEBUG=bat decode.

Counts 3DSTATE_DEPTH_BUFFER by (LOD, extent, HiZ enable) and 3DSTATE_WM_HZ_OP
by kind, tagged with the LOD of the most recent depth buffer and the clear
rectangle. In 3DSTATE_WM_HZ_OP, "Hierarchical Depth Buffer Resolve" is the
ambiguate, "Depth Buffer Resolve" is the full resolve and "Depth Buffer
Clear" is the fast clear. An op with no flags set is BLORP's terminating
packet and is counted separately.
"""
import collections
import re
import sys

HEADER = re.compile(r"^0x[0-9a-f]+:\s+0x[0-9a-f]+:\s+(\S+)")
FIELD = re.compile(r"^\s+([^:]+):\s*(.*?)\s*$")


def records(path):
    name, fields = None, {}
    with open(path, errors="replace") as f:
        for line in f:
            m = HEADER.match(line)
            if m:
                if name:
                    yield name, fields
                name, fields = m.group(1), {}
                continue
            m = FIELD.match(line)
            if m and name:
                fields.setdefault(m.group(1), m.group(2))
    if name:
        yield name, fields


def main():
    depth = collections.Counter()
    hz = collections.Counter()
    cur = ("?", "?", "?")
    for name, f in records(sys.argv[1]):
        if name == "3DSTATE_DEPTH_BUFFER":
            fmt = f.get("Surface Format", "?").split()[-1]
            if "NULL" in f.get("Surface Type", ""):
                cur = ("null", "-", "-")
                continue
            w = int(f.get("Width", "0")) + 1
            h = int(f.get("Height", "0")) + 1
            cur = (f.get("LOD", "?"), f"{w}x{h}", f.get("Hierarchical Depth Buffer Enable", "?"))
            depth[(fmt,) + cur] += 1
        elif name == "3DSTATE_WM_HZ_OP":
            kinds = [k for k, key in (("fast-clear", "Depth Buffer Clear Enable"),
                                      ("ambiguate", "Hierarchical Depth Buffer Resolve Enable"),
                                      ("full-resolve", "Depth Buffer Resolve Enable"),
                                      ("stencil-clear", "Stencil Buffer Clear Enable"))
                     if f.get(key) == "true"]
            kind = "+".join(kinds) or "end"
            rect = "({},{})-({},{})".format(*(f.get("Clear Rectangle " + c, "?") for c in
                                              ("X Min", "Y Min", "X Max", "Y Max")))
            full = "full-surface" if f.get("Full Surface Depth and Stencil Clear") == "true" else "partial"
            if kind == "end":
                hz[("end",)] += 1
            else:
                hz[(kind, "lod", cur[0], "lod0-extent", cur[1], "hiz", cur[2], rect, full)] += 1
    print("3DSTATE_DEPTH_BUFFER (format, LOD, LOD0 extent, HiZ enable): count")
    for k, v in sorted(depth.items()):
        print("  ", " ".join(k), ":", v)
    print("3DSTATE_WM_HZ_OP: count")
    for k, v in sorted(hz.items()):
        print("  ", " ".join(k), ":", v)


if __name__ == "__main__":
    main()
