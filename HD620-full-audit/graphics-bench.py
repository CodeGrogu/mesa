"""Benchmark independent Vulkan and GL graphics scenes and sample per-process i915 accounting."""
import csv
import json
import os
from pathlib import Path
import re
import subprocess
import time

root = Path(__file__).resolve().parent
tools = root / 'tools/root/usr'
evidence = root / 'evidence/graphics'
evidence.mkdir(parents=True, exist_ok=True)
libs = tools / 'lib/x86_64-linux-gnu'
clock = Path('/sys/class/drm/card1/gt/gt0/rps_act_freq_mhz')
env = {key: value for key, value in os.environ.items() if key not in
       {'VK_DRIVER_FILES', 'VK_ICD_FILENAMES', 'VK_ADD_DRIVER_FILES', 'ANV_DEBUG',
        'MESA_LOADER_DRIVER_OVERRIDE', 'GALLIUM_DRIVER', 'LIBGL_ALWAYS_SOFTWARE'}}
env['LD_LIBRARY_PATH'] = str(libs)
data = tools / 'share'
vk = [str(tools / 'bin/vkmark'), '--winsys', 'headless', '--winsys-dir',
      str(libs / 'vkmark'), '--data-dir', str(data / 'vkmark')]
gl = [str(tools / 'bin/glmark2'), '--data-path', str(data / 'glmark2'),
      '--off-screen', '--frame-end', 'finish']
provenance = json.loads((root.parent / 'HD620-benchmark/provenance.json').read_text())
drivers = {key: value['manifest'] for key, value in provenance.items()}
drivers['installed'] = '/usr/share/vulkan/icd.d/intel_icd.json'
vk_scenes = ('vertex:device-local=true', 'vertex:device-local=false',
             'shading:shading=phong', 'effect2d:kernel=blur')
gl_scenes = ('build:use-vbo=true', 'build:use-vbo=false', 'shading:shading=phong',
             'texture:texture-filter=mipmap')
PATTERN = re.compile(r'^(\[.+?)\s+FPS:\s+(\d+)\s+FrameTime:\s+(\S+) ms', re.M)

def snapshot(process, clients, clocks):
    for fd in Path(f'/proc/{process.pid}/fdinfo').glob('*'):
        try:
            values = dict(line.split(':', 1) for line in fd.read_text().splitlines() if ':' in line)
        except (OSError, ValueError):
            continue
        if values.get('drm-driver', '').strip() != 'i915':
            continue
        client_id = values['drm-client-id'].strip()
        client = clients.setdefault(client_id, {})
        for key, val in values.items():
            if key.startswith(('drm-engine-', 'drm-resident-')) and not key.startswith('drm-engine-capacity-'):
                client[key] = max(client.get(key, 0), int(val.split()[0]))
    try:
        clocks.append(int(clock.read_text()))
    except OSError:
        pass

def run(api, driver, iteration, args, scenes):
    path = evidence / f'{api}-{driver}-{iteration}.log'
    args += ['--size', '800x600']
    for scene in scenes:
        args += ['-b', f'{scene}:duration=2']
    run_env = env if api == 'gl' else env | {'VK_DRIVER_FILES': drivers[driver], 'VK_LOADER_DEBUG': 'driver'}
    clients, clocks = {}, []
    start = time.monotonic()
    with path.open('w') as log:
        process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, env=run_env)
        try:
            while process.poll() is None:
                if time.monotonic() - start > 50:
                    raise TimeoutError(str(path))
                snapshot(process, clients, clocks)
                time.sleep(.1)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
    log = path.read_text()
    assert process.returncode == 0 and 'HD Graphics 620' in log, path
    if driver in provenance:
        assert provenance[driver]['library'] in log, path
    totals = [(name.strip(), int(fps), float(ms)) for name, fps, ms in PATTERN.findall(log)]
    assert len(totals) == len(scenes), path
    (path.with_suffix('.json')).write_text(json.dumps({
        'i915_max_counters_per_client': clients, 'clock_samples_mhz': clocks,
        'wall_seconds': time.monotonic() - start}, indent=2) + '\n')
    return totals

with (evidence / 'timings.csv').open('w', newline='') as output:
    writer = csv.writer(output)
    writer.writerow(['api', 'driver', 'trial', 'scene', 'fps', 'frametime_ms'])
    for iteration in range(1, 5):
        order = ('original', 'patched', 'installed') if iteration % 2 else ('installed', 'patched', 'original')
        for driver in order:
            for name, fps, ms in run('vk', driver, iteration, vk.copy(), vk_scenes):
                writer.writerow(['vk', driver, iteration, name, fps, ms])
            output.flush()
    for iteration in range(1, 5):
        for name, fps, ms in run('gl', 'installed', iteration, gl.copy(), gl_scenes):
            writer.writerow(['gl', 'installed', iteration, name, fps, ms])
        output.flush()
