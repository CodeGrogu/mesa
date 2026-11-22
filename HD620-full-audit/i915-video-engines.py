"""Check which i915 engines execute each hardware media path."""
import json
import os
from pathlib import Path
import subprocess
import time

root = Path(__file__).resolve().parent
input_file = root.parent / 'HD620-benchmark/inputs/h264_1080_slices.mp4'
tests = {
    'decode': '',
    'scale': 'scale_vaapi=w=640:h=360',
    'denoise': 'denoise_vaapi=denoise=20',
}
for label, filters in tests.items():
    command = ['ffmpeg', '-hide_banner', '-nostdin', '-xerror', '-loglevel', 'info',
               '-threads', '2', '-filter_threads', '1', '-hwaccel', 'vaapi', '-hwaccel_device',
               '/dev/dri/renderD128', '-hwaccel_output_format', 'vaapi', '-i', str(input_file)]
    if filters:
        command += ['-vf', filters]
    command += ['-frames:v', '960', '-pix_fmt', '+vaapi', '-noauto_conversion_filters',
                '-c:v', 'wrapped_avframe', '-f', 'null', '-']
    with (root / f'evidence/media/engine-{label}.txt').open('w') as log:
        child = subprocess.Popen(command, env=os.environ | {'LIBVA_DRIVER_NAME': 'iHD'},
                                 stdout=log, stderr=subprocess.STDOUT)
        samples = {}
        try:
            deadline = time.monotonic() + 45
            while child.poll() is None and time.monotonic() < deadline:
                for path in Path(f'/proc/{child.pid}/fdinfo').glob('*'):
                    try:
                        fields = dict(line.split(':', 1) for line in path.read_text().splitlines() if ':' in line)
                    except OSError:
                        continue
                    if fields.get('drm-driver', '').strip() != 'i915':
                        continue
                    per_client = samples.setdefault(fields['drm-client-id'].strip(), {})
                    for key, value in fields.items():
                        if key.startswith('drm-engine-') and not key.startswith('drm-engine-capacity-'):
                            per_client[key] = max(per_client.get(key, 0), int(value.split()[0]))
                time.sleep(.03)
            if child.poll() is None:
                raise TimeoutError(label)
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=5)
    print(json.dumps({'path': label, 'returncode': child.returncode,
                      'max_per_client_ns': samples}, sort_keys=True))
