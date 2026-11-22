"""Check both drivers for identical 48-frame samples."""
import hashlib
from pathlib import Path
import subprocess

from benchmark import common, device, env, icds, jobs

out = Path(__file__).resolve().parent
for name, (_, args) in jobs.items():
    output_hashes = {}
    for driver in icds:
        command = (common + ['-loglevel', 'error'] + device + args
                   + ['-frames:v', '48', '-an'])
        command += ['-f', 'h264' if 'h264_vulkan' in args else 'rawvideo', '-']
        if 'h264_vulkan' not in args:
            command[command.index('wrapped_avframe')] = 'rawvideo'
        digest = hashlib.sha256()
        with subprocess.Popen(command, env=env | {'VK_DRIVER_FILES': str(icds[driver])},
                              stdout=subprocess.PIPE) as proc:
            while block := proc.stdout.read(1024 * 1024):
                digest.update(block)
            if proc.wait():
                raise RuntimeError(f'{name} failed on {driver}')
        output_hashes[driver] = digest.hexdigest()
    print(name, output_hashes, flush=True)
    assert output_hashes['original'] == output_hashes['patched'], name
print('PASS: all five decoded samples or encoded bitstreams match byte for byte.')
