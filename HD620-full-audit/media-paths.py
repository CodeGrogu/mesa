"""Check VA-API rate-control modes and sample engines for scaling policies."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parent
logs = root / 'evidence/hardware-paths'
logs.mkdir(parents=True, exist_ok=True)
env = os.environ | {'LIBVA_DRIVER_NAME': 'iHD'}
render = '/dev/dri/renderD128'
base = ['ffmpeg', '-hide_banner', '-nostdin', '-xerror', '-loglevel', 'info',
        '-threads', '2', '-filter_threads', '1']

with tempfile.TemporaryDirectory(prefix='hd620-va-rc-') as scratch:
    for codec, pixfmt, profile in [('h264_vaapi', 'nv12', 'high'),
                                   ('hevc_vaapi', 'nv12', 'main'),
                                   ('hevc_vaapi', 'p010le', 'main10')]:
        for mode in ('CQP', 'CBR', 'VBR'):
            name = f'{codec}-{profile}-{mode}'
            media = Path(scratch) / (name + '.mkv')
            bitrate = ['-qp', '24'] if mode == 'CQP' else [
                '-b:v', '2M', '-maxrate', '2M' if mode == 'CBR' else '3M', '-bufsize', '4M']
            args = base + ['-vaapi_device', render, '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=30',
                           '-vf', f'format={pixfmt},hwupload', '-frames:v', '60', '-c:v', codec,
                           '-profile:v', profile, '-rc_mode', mode, *bitrate, str(media)]
            args[args.index('info')] = 'debug'
            encoded = subprocess.run(args, env=env, capture_output=True, text=True, timeout=30)
            report = {'test': name, 'encode_rc': encoded.returncode}
            log = encoded.stderr
            if encoded.returncode == 0:
                decoded = subprocess.run(
                    ['ffprobe','-v','error','-select_streams','v:0','-count_frames',
                     '-show_entries','stream=profile,pix_fmt,nb_read_frames','-of','json',str(media)],
                    capture_output=True,text=True,timeout=15)
                if decoded.returncode == 0:
                    report['decoded'] = json.loads(decoded.stdout)['streams'][0]
                report['decode_rc'] = decoded.returncode
                log += decoded.stdout + decoded.stderr
            (logs / (name + '.log')).write_text(log)
            print(json.dumps(report), flush=True)

clip = root.parent / 'HD620-benchmark/inputs/h264_1080_slices.mp4'
for mode in ('default', 'fast', 'hq'):
    cmd = base + ['-hwaccel','vaapi','-hwaccel_device',render,'-hwaccel_output_format','vaapi','-i',str(clip),
                  '-vf',f'scale_vaapi=w=640:h=360:mode={mode}','-frames:v','960','-pix_fmt','+vaapi',
                  '-noauto_conversion_filters','-c:v','wrapped_avframe','-f','null','-']
    maxima = {}
    with (logs / ('scale-' + mode + '.log')).open('w') as out:
        with subprocess.Popen(cmd, env=env, stdout=out, stderr=subprocess.STDOUT) as proc:
            deadline = time.monotonic() + 35
            try:
                while proc.poll() is None:
                    if time.monotonic() > deadline:
                        raise TimeoutError(mode)
                    for fd in Path(f'/proc/{proc.pid}/fdinfo').glob('*'):
                        try:
                            body = fd.read_text()
                        except OSError:
                            continue
                        client = re.search(r'^drm-client-id:\s+(\d+)$', body, re.M)
                        if client and 'drm-driver:\ti915' in body:
                            values = maxima.setdefault(client[1], {})
                            for key, ns in re.findall(r'^drm-engine-([\w-]+):\s+(\d+) ns$',body,re.M):
                                values[key] = max(values.get(key,0),int(ns))
                    time.sleep(.02)
            finally:
                if proc.poll() is None:
                    proc.kill()
            print(json.dumps({'test': 'scale-' + mode, 'rc': proc.returncode, 'busy_ns': maxima}), flush=True)
