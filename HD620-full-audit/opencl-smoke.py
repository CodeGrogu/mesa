"""Verify device-backed Rusticl compilation, dispatch, output and profiling."""
import argparse
import ctypes as c
import statistics
import time
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--require-valid-profiling', action='store_true',
                    help='require the heavy kernel event to cover its GPU work')
args = parser.parse_args()

cl = c.CDLL("libOpenCL.so.1")
P, U, I, Z, Q = c.c_void_p, c.c_uint, c.c_int, c.c_size_t, c.c_uint64
PP = c.POINTER(P)

def bind(name, result, *params):
    func = getattr(cl, name)
    func.restype = result
    func.argtypes = params
    return func

def check(result):
    if result != 0:
        raise RuntimeError(f"OpenCL result {result}")

def make(func, *args):
    err = I()
    item = func(*args, c.byref(err))
    check(err.value)
    if not item:
        raise RuntimeError("OpenCL returned a null handle")
    return item

platforms = bind('clGetPlatformIDs', I, U, PP, c.POINTER(U))
devices = bind('clGetDeviceIDs', I, P, Q, U, PP, c.POINTER(U))
info = bind('clGetDeviceInfo', I, P, U, Z, P, c.POINTER(Z))
context = bind('clCreateContext', P, P, U, PP, P, P, c.POINTER(I))
queue = bind('clCreateCommandQueue', P, P, P, Q, c.POINTER(I))
program = bind('clCreateProgramWithSource', P, P, U, c.POINTER(c.c_char_p), P, c.POINTER(I))
build = bind('clBuildProgram', I, P, U, PP, c.c_char_p, P, P)
create_kernel = bind('clCreateKernel', P, P, c.c_char_p, c.POINTER(I))
buffer = bind('clCreateBuffer', P, P, Q, Z, P, c.POINTER(I))
arg = bind('clSetKernelArg', I, P, U, Z, P)
dispatch = bind('clEnqueueNDRangeKernel', I, P, P, U, P, c.POINTER(Z), c.POINTER(Z), U, PP, PP)
read = bind('clEnqueueReadBuffer', I, P, P, U, Z, Z, P, U, PP, PP)
prof = bind('clGetEventProfilingInfo', I, P, U, Z, P, c.POINTER(Z))

platform, device = P(), P()
check(platforms(1, c.byref(platform), None))
check(devices(platform, 4, 1, c.byref(device), None))

def device_text(field):
    raw = c.create_string_buffer(512)
    check(info(device, field, len(raw), raw, None))
    return raw.value.decode()

name, version = device_text(0x102B), device_text(0x102F)
print(name, version)
assert 'HD Graphics 620' in name
ctx = make(context, None, 1, c.byref(device), None, None)
cmd = make(queue, ctx, device, 2)
src = c.c_char_p(b'''__kernel void vector(__global const uint *in, __global uint *out) {
    size_t i = get_global_id(0);
    out[i] = in[i] * 7u + 3u;
}

__kernel void workload(__global const uint *in, __global uint *out, uint rounds) {
    size_t i = get_global_id(0);
    uint value = in[i];
    for (uint j = 0; j < rounds; ++j)
        value = (value * 1664525u + 1013904223u) ^ (value >> 7);
    out[i] = value;
}''')
prog = make(program, ctx, 1, c.byref(src), None)
status = build(prog, 1, c.byref(device), b'-cl-std=CL1.2', None, None)
if status:
    build_info = bind('clGetProgramBuildInfo', I, P, P, U, Z, P, c.POINTER(Z))
    message_size = Z()
    check(build_info(prog, device, 0x1183, 0, None, c.byref(message_size)))
    message = c.create_string_buffer(message_size.value)
    check(build_info(prog, device, 0x1183, len(message), message, None))
    print(message.value.decode())
    check(status)
count = 1 << 20
data = (U * count)(*range(count))
result = (U * count)()
src_mem = P(make(buffer, ctx, 4 | 32, c.sizeof(data), data))
dst_mem = P(make(buffer, ctx, 2, c.sizeof(result), None))
wait = bind('clWaitForEvents', I, U, PP)
release = bind('clReleaseEvent', I, P)
size = Z(count)

def render_busy_ns():
    counters = {}
    for fd in Path('/proc/self/fdinfo').glob('*'):
        try:
            fields = dict(line.split(':', 1) for line in fd.read_text().splitlines() if ':' in line)
        except OSError:
            continue
        if fields.get('drm-driver', '').strip() == 'i915':
            counters[fields['drm-client-id']] = int(fields['drm-engine-render'].split()[0])
    return sum(counters.values())

def benchmark(kernel_name, group_size, rounds=None):
    kern = make(create_kernel, prog, kernel_name)
    for i, item in enumerate([src_mem, dst_mem]):
        check(arg(kern, i, c.sizeof(P), c.byref(item)))
    if rounds is not None:
        n = U(rounds)
        check(arg(kern, 2, c.sizeof(U), c.byref(n)))
    local = Z(group_size)
    events, wall, busy = [], [], []
    for trial in range(10):
        done, start, end = P(), Q(), Q()
        before = render_busy_ns()
        t0 = time.monotonic_ns()
        check(dispatch(cmd, kern, 1, None, c.byref(size), c.byref(local), 0, None, c.byref(done)))
        check(wait(1, c.byref(done)))
        dt = time.monotonic_ns() - t0
        after = render_busy_ns()
        check(prof(done, 0x1282, c.sizeof(Q), c.byref(start), None))
        check(prof(done, 0x1283, c.sizeof(Q), c.byref(end), None))
        check(release(done))
        if trial > 0:
            events.append(end.value - start.value)
            wall.append(dt)
            busy.append(after - before)
    check(read(cmd, dst_mem, 1, 0, c.sizeof(result), result, 0, None, None))
    if rounds is None:
        assert all(actual == i * 7 + 3 for i, actual in enumerate(result))
        label = f'{count} exact results, local {group_size}'
    else:
        for i in range(0, count, 1021):
            value = i
            for _ in range(rounds):
                value = ((value * 1664525 + 1013904223) & 0xffffffff) ^ (value >> 7)
            assert result[i] == value
        label = f'{count} work-items, {rounds} dependent rounds, stride-1021 output checks'
    print(label)
    for metric, samples in (('event duration', events), ('enqueue and wait', wall), ('i915 render busy', busy)):
        print(f'  {metric:17} median {statistics.median(samples)/1e6:10.6f} ms'
              f'  samples ns {samples}')
    return statistics.median(events), statistics.median(wall), statistics.median(busy)

for group_size in (32, 64, 128, 256):
    benchmark(b'vector', group_size)
short_event, _, short_busy = benchmark(b'workload', 64, 16)
long_event, long_wall, long_busy = benchmark(b'workload', 64, 256)
if args.require_valid_profiling:
    # Loose bounds for this fixture on HD 620. They catch snapshots that
    # omit the dispatch without treating kernel fdinfo time as identical
    # to elapsed event time.
    assert short_busy > 0 and long_busy > 2 * short_busy, (short_busy, long_busy)
    assert 0 < short_event < long_event / 2, (short_event, long_event)
    assert long_busy / 2 <= long_event <= long_wall * 1.5, (long_busy, long_event, long_wall)
    print('PASS OpenCL event timestamps cover profiled compute')
