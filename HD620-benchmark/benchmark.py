"""Paired hardware Vulkan Video comparison using immutable driver snapshots."""
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import time

out = Path(__file__).resolve().parent
store = Path('/home/polarbear/.local/state/catchingup/hd620-anv-query-verification')
clips = store / 'patch8/media'
icds = {'original': store / 'patch6/icd/base-all/icd.json',
        'patched': store / 'patch8/icd/patch8-all/icd.json'}
common = ['ffmpeg', '-hide_banner', '-nostdin', '-xerror', '-filter_threads', '2']
device = ['-init_hw_device', 'vulkan=vk:0']
hwdecode = ['-hwaccel', 'vulkan', '-hwaccel_output_format', 'vulkan',
            '-hwaccel_device', 'vk', '-threads', '2']
def decode(fmt):
    return ['-vf', f'hwdownload,format={fmt}', '-c:v', 'wrapped_avframe']
encode = ['-c:v', 'h264_vulkan', '-rc_mode', 'cqp', '-qp', '24']
inputs = out / 'inputs'
jobs = {
    'H.264 decode · 1080p': (960, hwdecode + ['-i', str(inputs/'h264_1080_slices.mp4')] + decode('nv12')),
    'VP9 Profile 0 decode · 720p': (960, hwdecode + ['-i', str(inputs/'vp9_p0.webm')] + decode('nv12')),
    'VP9 Profile 2 decode · 720p': (960, hwdecode + ['-i', str(inputs/'vp9_p2.webm')] + decode('p010le')),
    'H.264 encode · 1080p': (480, ['-filter_hw_device', 'vk', '-f', 'lavfi', '-i',
                                   'testsrc2=size=1920x1080:rate=30', '-vf', 'format=nv12,hwupload'] + encode),
    'H.264 transcode · 1080p': (480, hwdecode + ['-i', str(inputs/'h264_1080_slices.mp4')] + encode),
}
env = {k: v for k, v in os.environ.items() if k not in {
    'VK_ICD_FILENAMES', 'VK_ADD_DRIVER_FILES', 'VK_LAYER_PATH', 'VK_INSTANCE_LAYERS',
    'VK_LOADER_DEBUG', 'anv_queue_override', 'ANV_DEBUG'}}
env['ANV_DEBUG'] = 'video-decode,video-encode'

def hash_file(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def run(driver, workload, frames, args, proof=False):
    config = env | {'VK_DRIVER_FILES': str(icds[driver])}
    library = json.loads(icds[driver].read_text())['ICD']['library_path']
    if proof:
        config['VK_LOADER_DEBUG'] = 'driver'
    command = (common + ['-loglevel', 'debug' if proof else 'info', '-nostats',
                         '-benchmark', '-progress', 'pipe:1'] + device + args
               + ['-frames:v', str(frames), '-an', '-f', 'null', '-'])
    start = time.perf_counter()
    p = subprocess.run(command, env=config, capture_output=True, text=True, timeout=90)
    seconds = time.perf_counter() - start
    if p.returncode:
        raise RuntimeError(f'{driver} {workload}: {p.stderr[-3500:]}')
    done = re.findall(r'^frame=\s*(\d+)', p.stdout, re.M)
    if not done or int(done[-1]) != frames or 'progress=end' not in p.stdout:
        raise RuntimeError(f'{driver} {workload} counted {done}, expected {frames}')
    if proof:
        name = list(jobs).index(workload) + 1
        (out / f'loader-{driver}-{name}.log').write_text(p.stderr)
        assert library in p.stderr and 'Device 0 selected: Intel(R) HD Graphics 620' in p.stderr
        if '-hwaccel' in args:
            assert 'Format vulkan chosen by get_format()' in p.stderr
        if 'h264_vulkan' in args:
            assert 'Using input frames context (format vulkan) with h264_vulkan encoder' in p.stderr
    bench = re.findall(r'utime=([\d.]+)s stime=([\d.]+)s rtime=([\d.]+)s', p.stderr)
    return seconds, (float(bench[-1][2]) if bench else None)

def main():
    inputs.mkdir(exist_ok=True)
    for clip in ('h264_1080_slices.mp4', 'vp9_p0.webm', 'vp9_p2.webm'):
        subprocess.run(['ffmpeg', '-hide_banner', '-nostdin', '-loglevel', 'error', '-y',
                        '-stream_loop', '19', '-i', str(clips / clip), '-map', '0:v:0',
                        '-c:v', 'copy', '-an', str(inputs / clip)], check=True)
    provenance = {}
    for driver, manifest in icds.items():
        lib = json.loads(manifest.read_text())['ICD']['library_path']
        provenance[driver] = {'manifest':str(manifest), 'library':lib, 'sha256':hash_file(lib)}
    (out/'provenance.json').write_text(json.dumps(provenance, indent=2)+'\n')
    rotations = 8
    with (out/'timings.csv').open('w', newline='') as f:
        row = csv.writer(f)
        row.writerow(['workload','pair','first_in_pair','driver','frames','wall_seconds','ffmpeg_seconds','wall_fps'])
        for i, (name, (frames, args)) in enumerate(jobs.items()):
            for driver in icds:
                run(driver, name, 48, args, proof=True)
                run(driver, name, frames, args)
            for pair in range(rotations):
                order = ('original','patched') if (i+pair)%2 == 0 else ('patched','original')
                for driver in order:
                    wall, intrinsic = run(driver, name, frames, args)
                    row.writerow([name,pair+1,order[0],driver,frames,f'{wall:.9f}',intrinsic,f'{frames/wall:.6f}'])
                    f.flush()
            print(f'{name}: {rotations} pairs complete', flush=True)
    print('All runs verified', flush=True)

if __name__ == '__main__':
    main()
