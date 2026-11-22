"""Compare reported ANV video support and unrelated extension parity."""
import json
from pathlib import Path
import subprocess

out = Path(__file__).resolve().parent
modes = {
    'default': ('', ''),
    'decode': ('video-decode', ''),
    'encode': ('video-encode', ''),
    'both': ('video-decode,video-encode', ''),
    'engine hidden': ('video-decode,video-encode', 'v=0'),
}
results = {}
for codecs in ('all', 'free'):
    for driver, prefix in (('original','base'), ('patched','patch8')):
        snapshot = f'{prefix}-{codecs}'
        lib = f'/build/snap/{snapshot}/libvulkan_intel.so'
        digest = subprocess.check_output(['docker','exec','hd620-mesa','sha256sum',lib], text=True).split()[0]
        for mode, (debug, override) in modes.items():
            cmd = ['docker','exec', '-e', 'VK_LOADER_DEBUG=driver',
                   '-e', f'VK_DRIVER_FILES=/build/snap/{snapshot}/icd.json',
                   '-e', f'ANV_DEBUG={debug}']
            if override:
                cmd += ['-e', f'anv_queue_override={override}']
            p = subprocess.run(cmd + ['hd620-mesa','python3','-I','/build/p2probe.py'],
                               text=True, capture_output=True, check=True)
            assert lib in p.stderr, p.stderr
            result = json.loads(p.stdout)
            result['sha256'] = digest
            result['loader'] = [line.strip() for line in p.stderr.splitlines() if lib in line]
            results[f'{codecs}/{driver}/{mode}'] = result
for codecs in ('all', 'free'):
    for mode in modes:
        originals = results[f'{codecs}/original/{mode}']['exts']
        modified = results[f'{codecs}/patched/{mode}']['exts']
        assert {e for e in originals if not e.startswith('VK_KHR_video_')} == {
            e for e in modified if not e.startswith('VK_KHR_video_')}, (codecs, mode)
(out/'capabilities.json').write_text(json.dumps(results, indent=2)+'\n')
for codecs in ('all','free'):
    for mode in modes:
        print(f'{codecs} / {mode}')
        for driver in ('original','patched'):
            p = results[f'{codecs}/{driver}/{mode}']
            vexts = [name.removeprefix('VK_KHR_video_') for name in p['exts'] if name.startswith('VK_KHR_video_')]
            qs = [(hex(q['flags']), None if q['mask'] is None else hex(q['mask']))
                  for q in p['queues'] if q['flags'] & 0x60]
            print(f'  {driver}: video extensions {vexts}; video queue [directions, codec mask] {qs}; '
                  f'NV12 video flags {p["nv12_optimal"] & 0x1e000000:#x}; {len(p["exts"])} total extensions')
