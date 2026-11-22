"""Measure the cost of downloading hardware-decoded frames on this host."""
import csv
import json
from pathlib import Path
import os
import re
import statistics
import subprocess
import time

out = Path(__file__).resolve().parent
root = out.parent
patched = json.loads((root / 'provenance.json').read_text())['patched']
env = {k:v for k,v in os.environ.items() if k not in ('VK_ICD_FILENAMES','VK_ADD_DRIVER_FILES',
                                                    'ANV_DEBUG','anv_queue_override','VK_LOADER_DEBUG')}
env.update(VK_DRIVER_FILES=patched['manifest'], ANV_DEBUG='video-decode,video-encode')
clips = {'H.264 1080p': ('h264_1080_slices.mp4','nv12'),
         'VP9 10-bit 720p': ('vp9_p2.webm','p010le')}
frames = 960

def cmd(clip, fmt, download, count=frames, proof=False):
    args = ['ffmpeg', '-hide_banner','-nostdin','-xerror','-v','debug' if proof else 'error',
            '-nostats','-progress','pipe:1','-filter_threads','2', '-init_hw_device','vulkan=vk:0',
            '-hwaccel','vulkan','-hwaccel_output_format','vulkan','-hwaccel_device','vk',
            '-threads','2','-i',str(root/'inputs'/clip)]
    args += ['-vf',f'hwdownload,format={fmt}'] if download else ['-pix_fmt','+vulkan','-noauto_conversion_filters']
    return args+['-c:v','wrapped_avframe','-frames:v',str(count),'-an','-f','null','-']

def run(clip, fmt, download, count=frames, proof=False):
    start=time.perf_counter()
    p=subprocess.run(cmd(clip,fmt,download,count,proof), env=env|({'VK_LOADER_DEBUG':'driver'} if proof else {}),
                     capture_output=True,text=True,timeout=60)
    seconds=time.perf_counter()-start
    done=re.findall(r'^frame=\s*(\d+)',p.stdout,re.M)
    assert p.returncode==0 and done and int(done[-1])==count and 'progress=end' in p.stdout, p.stderr[-1700:]
    if proof:
        assert patched['library'] in p.stderr and 'Device 0 selected: Intel(R) HD Graphics 620' in p.stderr
        assert 'Format vulkan chosen by get_format()' in p.stderr
        assert ('Video: wrapped_avframe, ' + ('1 reference frame, ' if clip.endswith('.mp4') else '')) in p.stderr
        if download:
            assert 'Parsed_hwdownload_' in p.stderr
        else:
            assert re.search(r'Video: wrapped_avframe[^\n]*\bvulkan\(',p.stderr)
        (out/f'{Path(clip).stem}-{"download" if download else "gpu"}.log').write_text(p.stderr)
    return seconds

summary=[]
with (out/'timings.csv').open('w',newline='') as f:
    writer=csv.writer(f)
    writer.writerow(['workload','pair','path','frames','wall_seconds','fps'])
    for label,(clip,fmt) in clips.items():
        for flag in (False,True):
            run(clip,fmt,flag,120,True)
            run(clip,fmt,flag)
        pairs=[]
        for i in range(8):
            p={}
            for flag in ((False,True) if i%2==0 else (True,False)):
                t=run(clip,fmt,flag)
                path='download' if flag else 'gpu'
                p[path]=t
                writer.writerow([label,i+1,path,frames,f'{t:.9f}',f'{frames/t:.3f}'])
                f.flush()
            pairs.append(p)
        fps={k:statistics.median(frames/p[k] for p in pairs) for k in ('gpu','download')}
        saved=[1000*(p['download']-p['gpu'])/frames for p in pairs]
        change=[100*(p['download']/p['gpu']-1) for p in pairs]
        summary.append(dict(workload=label, **fps, median_ms_per_frame=statistics.median(saved),
                            median_throughput_gain_pct=statistics.median(change),
                            min_throughput_gain_pct=min(change), max_throughput_gain_pct=max(change)))
        print(summary[-1],flush=True)
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
