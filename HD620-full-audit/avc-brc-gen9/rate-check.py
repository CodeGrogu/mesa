"""Bitrate and VBV check of an H.264 elementary stream / container.

usage: rate-check.py FILE FPS TARGET_BPS BUFSIZE_BITS [INIT_FULLNESS_BITS]
Prints overall bitrate, per-second min/max, and a leaky-bucket (VBV)
simulation: the buffer fills at TARGET_BPS and each frame removes its bits
at its decode time.  Underflow = a frame larger than the buffer content.
"""
import json, subprocess, sys
path, fps, target, bufsize = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
init = float(sys.argv[5]) if len(sys.argv) > 5 else bufsize
out = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                      'packet=size,flags', '-of', 'json', path], capture_output=True, text=True, check=True)
pk = json.loads(out.stdout)['packets']
sizes = [int(p['size']) * 8 for p in pk]
n = len(sizes)
dur = n / fps
total = sum(sizes)
per_sec = [sum(sizes[int(i * fps):int((i + 1) * fps)]) for i in range(int(dur))]
fill = init
under = over = 0
worst = bufsize
for s in sizes:
    if s > fill:
        under += 1
    fill = max(fill - s, 0)
    worst = min(worst, fill)
    fill += target / fps
    if fill > bufsize:
        over += 1
        fill = bufsize
keys = sum(1 for p in pk if 'K' in p['flags'])
print(f'{n} frames, {keys} keyframes, {dur:.1f} s: {total / dur / 1e6:.3f} Mbit/s '
      f'(target {target / 1e6:.3f}, {100 * (total / dur / target - 1):+.1f}%)')
if per_sec:
    print(f'  per-second Mbit/s: min {min(per_sec) / 1e6:.3f} max {max(per_sec) / 1e6:.3f}')
print(f'  VBV {bufsize / 1e6:.2f} Mbit: {under} underflows, {over} frames at full buffer, '
      f'lowest fullness {worst / bufsize * 100:.0f}%')
