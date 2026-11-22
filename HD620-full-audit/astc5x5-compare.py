"""Compare two gl-astc5x5.py outputs (4 cases x 128x128 RGBA8)."""
import sys
a, b = (open(p, 'rb').read() for p in sys.argv[1:3])
assert len(a) == len(b) == 4 * 128 * 128 * 4
names = ['astc', 'both', 'alt', 'compute']
worst = 0
for i, name in enumerate(names):
    x, y = a[i * 65536:(i + 1) * 65536], b[i * 65536:(i + 1) * 65536]
    diffs = [abs(p - q) for p, q in zip(x, y)]
    m = max(diffs)
    worst = max(worst, m)
    print(f'{name:8s} max channel diff {m:3d}, channels differing {sum(d > 0 for d in diffs):6d} of {len(diffs)}, '
          f'sample {list(x[4000:4004])} vs {list(y[4000:4004])}')
limit = int(sys.argv[3]) if len(sys.argv) > 3 else 1
print('PASS' if worst <= limit else 'FAIL', f'(limit {limit})')
sys.exit(worst > limit)
