"""Measure VA versus software decoded pixel differences in matching NV12 layouts."""
import math
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent / 'evidence/media'
env = os.environ | {'LIBVA_DRIVER_NAME': 'iHD'}
for name, color_range in [('mpeg2_vaapi-nv12.mpeg2video', 'tv'), ('mjpeg_vaapi-nv12.mjpeg', 'pc')]:
    decoded = {}
    for key in ('hardware', 'software'):
        common = ['ffmpeg', '-hide_banner', '-nostdin', '-xerror', '-loglevel', 'error',
                  '-threads', '2', '-filter_threads', '1']
        acceleration = ['-hwaccel', 'vaapi', '-hwaccel_device', '/dev/dri/renderD128',
                        '-hwaccel_output_format', 'vaapi'] if key == 'hardware' else []
        filters = 'hwdownload,format=nv12' if key == 'hardware' else 'format=nv12'
        args = common + acceleration + ['-i', str(root / name), '-vf', filters, '-color_range',
                                         color_range, '-c:v', 'rawvideo', '-pix_fmt', 'nv12',
                                         '-frames:v', '60', '-f', 'rawvideo', '-']
        decoded[key] = subprocess.run(args, env=env, check=True, capture_output=True).stdout
    hw, sw = decoded['hardware'], decoded['software']
    size = 60 * 640 * 360 * 3 // 2
    assert len(hw) == len(sw) == size
    hist = [0] * 256
    for a, b in zip(hw, sw):
        hist[abs(a-b)] += 1
    mse = sum(i*i*n for i, n in enumerate(hist)) / size
    psnr = 10 * math.log10(255*255 / mse) if mse else math.inf
    print(f'{name} {size} samples, full-buffer PSNR {psnr:.3f} dB')
    print('absolute pixel differences:', [(i, n) for i, n in enumerate(hist) if n])
