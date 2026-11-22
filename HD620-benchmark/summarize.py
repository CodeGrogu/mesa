"""Summarize the recorded paired wall-clock measurements."""
import csv
import json
from pathlib import Path
import statistics

out = Path(__file__).resolve().parent
runs = {}
for row in csv.DictReader((out/'timings.csv').open()):
    runs.setdefault(row['workload'], {}).setdefault(int(row['pair']), {})[row['driver']] = float(row['wall_fps'])
assert len(runs) == 5
stats = []
for job, pairs in runs.items():
    assert set(pairs) == set(range(1, 9))
    assert all(set(pair) == {'original','patched'} for pair in pairs.values())
    original = [pair['original'] for pair in pairs.values()]
    patched = [pair['patched'] for pair in pairs.values()]
    deltas = [100*(p/o - 1) for o,p in zip(original,patched)]
    d = {'job':job, 'original_fps':statistics.median(original),
         'patched_fps':statistics.median(patched), 'delta_pct':statistics.median(deltas),
         'pair_delta_min':min(deltas), 'pair_delta_max':max(deltas),
         'original_range': [min(original),max(original)], 'patched_range':[min(patched),max(patched)]}
    stats.append(d)
    print(job, f"{d['original_fps']:.1f} -> {d['patched_fps']:.1f} fps",
          f"paired median {d['delta_pct']:+.2f}% ({min(deltas):+.2f}% to {max(deltas):+.2f}%)")
(out/'summary.json').write_text(json.dumps(stats, indent=2)+'\n')
