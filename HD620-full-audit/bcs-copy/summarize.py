#!/usr/bin/env python3
"""Tabulate i915-bcs-copy / vulkan-render-copy JSON lines as Markdown. Read-only."""
import json
import sys

COLS = ("route", "packet", "mode", "size", "copies", "subs", "host p50 us", "host p95 us",
        "gpu p50 us", "host GB/s", "gpu GB/s", "aggregate GB/s", "engine us/sample",
        "busy", "MHz", "verified")


def size(b):
    for unit, shift in (("M", 20), ("K", 10)):
        if b >= 1 << shift and b % (1 << shift) == 0:
            return f"{b >> shift}{unit}"
    return str(b)


def row(r):
    us = lambda k, p: f"{r[k][p] / 1e3:.1f}" if k in r else "-"
    engine = r.get("fdinfo_copy_ns") if r["route"] == "bcs" else r.get("fdinfo_render_ns")
    busy = r.get("copy_busy_vs_host", r.get("render_busy_vs_host"))
    return (r["route"], r["packet"], r["mode"], size(r["size_bytes"]), r["copies_per_sample"],
            r["submissions_per_sample"], us("host_ns", "p50"), us("host_ns", "p95"),
            us("gpu_span_ns", "p50"), f"{r['host_GBps_p50']:.2f}",
            f"{r['gpu_GBps_p50']:.2f}" if "gpu_GBps_p50" in r else "-",
            f"{r['host_GBps_aggregate']:.2f}" if "host_GBps_aggregate" in r else "-",
            f"{engine / r['samples'] / 1e3:.1f}" if engine is not None else "-",
            f"{busy:.2f}" if busy is not None else "-",
            f"{r.get('freq_mhz_before', '-')}/{r.get('freq_mhz_after', '-')}",
            "ok" if r["bad_words"] == 0 else f"BAD {r['bad_words']}")


def main(paths):
    failures = 0
    print("| " + " | ".join(COLS) + " |")
    print("|" + "---|" * len(COLS))
    for path in paths:
        with open(path) as f:
            for line in f:
                r = json.loads(line)
                if r["record"] == "result":
                    print("| " + " | ".join(str(c) for c in row(r)) + " |")
                elif r["record"] == "summary":  # already includes mismatch records
                    failures += r["failures"]
    print(f"\nFailures (mismatches + engine-accounting): {failures}. Times are ns-derived; "
          "GB/s = 1e9 bytes/s; gpu = in-batch timestamp span; busy = fdinfo engine ns / host ns.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
