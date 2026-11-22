"""Check that Iris query-buffer timer results equal glGetQueryObject results.

Iris resolves a query written to a GL_QUERY_BUFFER in one of two ways.  If
the snapshots have already landed, it copies the CPU-computed result
(calculate_result_on_cpu).  Otherwise it records MI_MATH that computes the
result on the GPU (calculate_result_on_gpu).  Each case below forces one of
the two paths and compares the buffer contents with the client-memory
result of glGetQueryObject*, which always uses the CPU path.

GPU path: the query ends right after long draw or compute work, and the
resolve is recorded immediately, so the snapshot has not landed yet.
CPU path: glFinish before the resolve.

Checked: GL_TIMESTAMP and GL_TIME_ELAPSED, 64- and 32-bit buffer results,
GL_QUERY_RESULT (waits), GL_QUERY_RESULT_NO_WAIT (predicated store) and
GL_QUERY_RESULT_AVAILABLE.  With --wrap SECONDS the probe loops for that
long and reports intervals that cross the 36-bit timestamp rollover.
Exit status is nonzero when any exact-equality check fails.
"""
import ctypes as c
import os
import statistics
import sys
import time

os.environ['EGL_PLATFORM'] = 'surfaceless'
egl = c.CDLL(os.environ.get('EGL_TEST_LIBRARY', 'libEGL.so.1'))
P, U, I, Q, Z = c.c_void_p, c.c_uint, c.c_int, c.c_uint64, c.c_ssize_t
PI, PU = c.POINTER(I), c.POINTER(U)
get_proc_address = egl.eglGetProcAddress
get_proc_address.restype, get_proc_address.argtypes = P, (c.c_char_p,)
gl = object()


def bind(lib, name, ret, *args):
    if lib is gl:
        address = get_proc_address(name.encode())
        if not address:
            raise RuntimeError(f'EGL did not provide {name}')
        return c.CFUNCTYPE(ret, *args)(address)
    f = getattr(lib, name)
    f.restype, f.argtypes = ret, args
    return f


display = bind(egl, 'eglGetDisplay', P, P)(None)
assert display and bind(egl, 'eglInitialize', U, P, PI, PI)(display, None, None)
assert bind(egl, 'eglBindAPI', U, U)(0x30A2)
config, configs = P(), I()
attrs = (I * 5)(0x3040, 8, 0x3033, 1, 0x3038)
assert bind(egl, 'eglChooseConfig', U, P, PI, c.POINTER(P), I, PI)(
    display, attrs, c.byref(config), 1, c.byref(configs)) and configs.value
attrs = (I * 7)(0x3098, 4, 0x30FB, 3, 0x30FD, 1, 0x3038)
context = bind(egl, 'eglCreateContext', P, P, P, P, PI)(display, config, None, attrs)
assert context and bind(egl, 'eglMakeCurrent', U, P, P, P, P)(display, None, None, context)
get_string = bind(gl, 'glGetString', c.c_char_p, U)
renderer = get_string(0x1F01).decode()
assert 'HD Graphics 620' in renderer
print(renderer, get_string(0x1F02).decode())
with open('/proc/self/maps') as maps:
    paths = {line.split()[-1] for line in maps if '/libEGL.so.' in line or '/libgallium-' in line}
for path in sorted(paths):
    print(f'Mapped driver library {path}')

create_shader = bind(gl, 'glCreateShader', U, U)
shader_source = bind(gl, 'glShaderSource', None, U, I, c.POINTER(c.c_char_p), PI)
compile_shader = bind(gl, 'glCompileShader', None, U)
get_shader = bind(gl, 'glGetShaderiv', None, U, U, PI)
shader_log = bind(gl, 'glGetShaderInfoLog', None, U, I, PI, c.c_char_p)
create_program = bind(gl, 'glCreateProgram', U)
attach = bind(gl, 'glAttachShader', None, U, U)
link = bind(gl, 'glLinkProgram', None, U)
get_program = bind(gl, 'glGetProgramiv', None, U, U, PI)
use = bind(gl, 'glUseProgram', None, U)
uniform_location = bind(gl, 'glGetUniformLocation', I, U, c.c_char_p)
uniform = bind(gl, 'glUniform1ui', None, I, U)


def program(*stages):
    prog = create_program()
    for kind, text in stages:
        shader = create_shader(kind)
        src = c.c_char_p(text.encode())
        shader_source(shader, 1, c.byref(src), None)
        compile_shader(shader)
        status = I()
        get_shader(shader, 0x8B81, c.byref(status))
        if not status.value:
            log = c.create_string_buffer(4096)
            shader_log(shader, 4096, None, log)
            raise RuntimeError(log.value.decode())
        attach(prog, shader)
    link(prog)
    status = I()
    get_program(prog, 0x8B82, c.byref(status))
    assert status.value
    location = uniform_location(prog, b'rounds')
    assert location >= 0
    return prog, location


HASH = '''
uint hash(uint value, uint rounds) {
    for (uint j = 0; j < rounds; ++j)
        value = (value * 1664525u + 1013904223u) ^ (value >> 7);
    return value;
}
'''
compute_prog, compute_rounds = program((0x91B9, '''#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Output { uint values[]; };
uniform uint rounds;
''' + HASH + '''
void main() { values[gl_GlobalInvocationID.x] = hash(gl_GlobalInvocationID.x, rounds); }
'''))
draw_prog, draw_rounds = program((0x8B31, '''#version 430
void main() {
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
'''), (0x8B30, '''#version 430
uniform uint rounds;
out vec4 color;
''' + HASH + '''
void main() {
    uint v = hash(uint(gl_FragCoord.x) * 4096u + uint(gl_FragCoord.y), rounds);
    color = vec4(float(v & 255u) / 255.0, 0.0, 0.0, 1.0);
}
'''))


def cpu_hash(value, rounds):
    for _ in range(rounds):
        value = ((value * 1664525 + 1013904223) & 0xffffffff) ^ (value >> 7)
    return value


gen = lambda name: bind(gl, name, None, I, PU)
gen_buffers, gen_queries = gen('glGenBuffers'), gen('glGenQueries')
gen_textures, gen_fbos, gen_vaos = gen('glGenTextures'), gen('glGenFramebuffers'), gen('glGenVertexArrays')
bind_buffer = bind(gl, 'glBindBuffer', None, U, U)
bind_base = bind(gl, 'glBindBufferBase', None, U, U, U)
buffer_data = bind(gl, 'glBufferData', None, U, Z, P, U)
read_buffer = bind(gl, 'glGetBufferSubData', None, U, Z, Z, P)
dispatch_compute = bind(gl, 'glDispatchCompute', None, U, U, U)
barrier = bind(gl, 'glMemoryBarrier', None, U)
draw_arrays = bind(gl, 'glDrawArrays', None, U, I, I)
finish = bind(gl, 'glFinish', None)
error = bind(gl, 'glGetError', U)
begin = bind(gl, 'glBeginQuery', None, U, U)
end = bind(gl, 'glEndQuery', None, U)
stamp = bind(gl, 'glQueryCounter', None, U, U)
query_result = bind(gl, 'glGetQueryObjectui64v', None, U, U, c.POINTER(Q))
query_to_buffer = bind(gl, 'glGetQueryObjectui64v', None, U, U, P)

NUM = 1 << 20
SIZE = 1024
TIME_ELAPSED, TIMESTAMP, RESULT = 0x88BF, 0x8E28, 0x8866
SSBO, QUERY_BUFFER = 0x90D2, 0x9192

ssbo, qbo, tex, fbo, vao = U(), U(), U(), U(), U()
gen_buffers(1, c.byref(ssbo))
bind_base(SSBO, 0, ssbo.value)
buffer_data(SSBO, NUM * 4, None, 0x88E8)
gen_buffers(1, c.byref(qbo))
bind_buffer(QUERY_BUFFER, qbo.value)
buffer_data(QUERY_BUFFER, 64, None, 0x88E8)
bind_buffer(QUERY_BUFFER, 0)
gen_textures(1, c.byref(tex))
bind(gl, 'glBindTexture', None, U, U)(0x0DE1, tex.value)
bind(gl, 'glTexStorage2D', None, U, I, U, I, I)(0x0DE1, 1, 0x8058, SIZE, SIZE)
gen_fbos(1, c.byref(fbo))
bind(gl, 'glBindFramebuffer', None, U, U)(0x8D40, fbo.value)
bind(gl, 'glFramebufferTexture2D', None, U, U, U, U, I)(0x8D40, 0x8CE0, 0x0DE1, tex.value, 0)
assert bind(gl, 'glCheckFramebufferStatus', U, U)(0x8D40) == 0x8CD5
bind(gl, 'glViewport', None, I, I, I, I)(0, 0, SIZE, SIZE)
gen_vaos(1, c.byref(vao))
bind(gl, 'glBindVertexArray', None, U)(vao.value)
read_pixels = bind(gl, 'glReadPixels', None, I, I, I, I, U, U, P)
queries = (U * 16)()
gen_queries(16, queries)
assert error() == 0


def compute(rounds, items=NUM):
    use(compute_prog)
    uniform(compute_rounds, rounds)
    dispatch_compute(items // 64, 1, 1)
    barrier(0x200)


def draw(rounds):
    use(draw_prog)
    uniform(draw_rounds, rounds)
    draw_arrays(0x0004, 0, 3)


def check_outputs(c_rounds, g_rounds, items=NUM):
    finish()
    out = (U * NUM)()
    read_buffer(SSBO, 0, NUM * 4, out)
    for i in list(range(0, items, 4093)) + [items - 1]:
        assert out[i] == cpu_hash(i, c_rounds), i
    px = (c.c_ubyte * 4)()
    for x, y in ((0, 0), (517, 311), (SIZE - 1, SIZE - 1)):
        read_pixels(x, y, 1, 1, 0x1908, 0x1401, px)
        assert px[0] == cpu_hash(x * 4096 + y, g_rounds) & 255, (x, y, px[0])
    assert error() == 0


def result(q):
    value = Q()
    query_result(q, RESULT, c.byref(value))
    return value.value


def drained(work):
    """GPU time of work, isolated with glFinish on both sides."""
    finish()
    stamp(queries[0], TIMESTAMP)
    finish()
    work()
    finish()
    stamp(queries[1], TIMESTAMP)
    finish()
    return result(queries[1]) - result(queries[0])


def elapsed(before, work, after):
    before()
    begin(TIME_ELAPSED, queries[2])
    work()
    end(TIME_ELAPSED)
    after()
    finish()
    return result(queries[2])


def pair(before, work, after):
    before()
    stamp(queries[3], TIMESTAMP)
    work()
    stamp(queries[4], TIMESTAMP)
    after()
    finish()
    return result(queries[4]) - result(queries[3])


def pair_qbo(before, work, after):
    """Timestamp pair resolved into a query buffer (get_query_result_resource).

    Returns (query-buffer delta, glGetQueryObject delta, buffer values match)."""
    before()
    stamp(queries[5], TIMESTAMP)
    work()
    stamp(queries[6], TIMESTAMP)
    after()
    bind_buffer(QUERY_BUFFER, qbo.value)
    query_to_buffer(queries[5], RESULT, P(0))
    query_to_buffer(queries[6], RESULT, P(8))
    bind_buffer(QUERY_BUFFER, 0)
    finish()
    out = (Q * 2)()
    bind_buffer(0x8F36, qbo.value)
    read_buffer(0x8F36, 0, 16, out)
    bind_buffer(0x8F36, 0)
    cpu = (result(queries[5]), result(queries[6]))
    return out[1] - out[0], cpu[1] - cpu[0], (out[0], out[1]) == cpu


def chain():
    """Four timestamps around compute, draw, compute; return three deltas."""
    stamp(queries[7], TIMESTAMP)
    compute(C_ROUNDS)
    stamp(queries[8], TIMESTAMP)
    draw(G_ROUNDS)
    stamp(queries[9], TIMESTAMP)
    compute(C_ROUNDS)
    stamp(queries[10], TIMESTAMP)
    finish()
    t = [result(queries[i]) for i in (7, 8, 9, 10)]
    assert t == sorted(t), f'timestamps not monotonic {t}'
    return [t[1] - t[0], t[2] - t[1], t[3] - t[2]]


def median(fn, trials=9):
    fn()
    return statistics.median(fn() for _ in range(trials))



NO_WAIT, AVAILABLE = 0x9194, 0x8867
query_to_buffer_u32 = bind(gl, 'glGetQueryObjectuiv', None, U, U, P)
query_result_u32 = bind(gl, 'glGetQueryObjectuiv', None, U, U, PU)
buffer_sub_data = bind(gl, 'glBufferSubData', None, U, Z, Z, P)
COPY_READ = 0x8F36
SENTINEL = 0x5A5A5A5A5A5A5A5A


def qbo_reset():
    bind_buffer(QUERY_BUFFER, qbo.value)
    fill = (Q * 8)(*([SENTINEL] * 8))
    buffer_sub_data(QUERY_BUFFER, 0, 64, fill)
    bind_buffer(QUERY_BUFFER, 0)


def qbo_resolve(q, pname=RESULT, offset=0, bits=64):
    bind_buffer(QUERY_BUFFER, qbo.value)
    (query_to_buffer if bits == 64 else query_to_buffer_u32)(q, pname, P(offset))
    bind_buffer(QUERY_BUFFER, 0)


def qbo_read():
    finish()
    out = (Q * 8)()
    bind_buffer(COPY_READ, qbo.value)
    read_buffer(COPY_READ, 0, 64, out)
    bind_buffer(COPY_READ, 0)
    return list(out)


def client_u32(q):
    value = U()
    query_result_u32(q, RESULT, c.byref(value))
    return value.value


heavy_c = lambda: compute(256)
heavy_g = lambda: draw(256)
nothing = lambda: None
failures = []


def report(name, ok, detail):
    print(f'{"PASS" if ok else "FAIL"} {name}: {detail}')
    if not ok:
        failures.append(name)


def run_case(kind, work, gpu_path, runs=9):
    """Resolve one query into the buffer at offsets 0 (u64), 8 (u32), 16 (NO_WAIT u64), 24 (AVAILABLE u32).

    Returns per-run tuples (buffer u64, buffer u32, no-wait u64, available, client u64, client u32)."""
    rows = []
    for _ in range(runs):
        qbo_reset()
        finish()
        # A query object keeps the target it was first used with.
        q = queries[2] if kind == 'timestamp' else queries[12]
        if kind == 'timestamp':
            work()
            stamp(q, TIMESTAMP)
        else:
            begin(TIME_ELAPSED, q)
            work()
            end(TIME_ELAPSED)
        if not gpu_path:
            finish()
        qbo_resolve(q, NO_WAIT, 16)
        qbo_resolve(q, RESULT, 0)
        qbo_resolve(q, RESULT, 8, bits=32)
        qbo_resolve(q, AVAILABLE, 24, bits=32)
        out = qbo_read()
        rows.append((out[0], out[1] & 0xffffffff, out[2], out[3] & 0xffffffff, result(q), client_u32(q)))
        assert out[1] >> 32 == SENTINEL >> 32, 'u32 store wrote beyond 4 bytes'
        assert error() == 0
    return rows


def check_case(name, rows, expect_nonzero=True):
    exact = sum(r[0] == r[4] for r in rows)
    report(f'{name}: 64-bit buffer == glGetQueryObjectui64v', exact == len(rows),
           f'{exact}/{len(rows)} exact; buffer-minus-client ns {[r[0] - r[4] for r in rows]}')
    if os.environ.get('IRIS_TEST_LEGACY_QUERY_TRUNCATION') == '1':
        u32_exact = sum(r[1] == (r[4] & 0xffffffff) for r in rows)
        u32_reference = 'legacy low 32 bits of the 64-bit result'
    else:
        u32_exact = sum(r[1] == r[5] for r in rows)
        u32_reference = 'glGetQueryObjectuiv'
    report(f'{name}: 32-bit buffer == {u32_reference}', u32_exact == len(rows),
           f'{u32_exact}/{len(rows)}; client glGetQueryObjectuiv {sorted(set(r[5] for r in rows))[:3]}')
    nowait = [r[2] for r in rows]
    ok = all(v in (SENTINEL, r[4]) for v, r in zip(nowait, rows))
    report(f'{name}: NO_WAIT buffer is the exact result or untouched', ok,
           f'{sum(v != SENTINEL for v in nowait)}/{len(rows)} written')
    report(f'{name}: AVAILABLE buffer == 1', all(r[3] == 1 for r in rows), f'{[r[3] for r in rows]}')
    if expect_nonzero:
        report(f'{name}: result is nonzero', all(r[4] > 0 for r in rows),
               f'median client {statistics.median(r[4] for r in rows)} ns')


cases = [
    ('timestamp after draw, GPU path', 'timestamp', heavy_g, True),
    ('timestamp after compute, GPU path', 'timestamp', heavy_c, True),
    ('timestamp, CPU path', 'timestamp', heavy_g, False),
    ('time elapsed of draw, GPU path', 'elapsed', heavy_g, True),
    ('time elapsed of compute, GPU path', 'elapsed', heavy_c, True),
    ('time elapsed of draw+compute, GPU path', 'elapsed', lambda: (heavy_g(), heavy_c()), True),
    ('time elapsed of draw, CPU path', 'elapsed', heavy_g, False),
    ('time elapsed of nothing, GPU path', 'elapsed', nothing, True),
]


def pair_mixed(runs=9):
    """Timestamp pair: first resolved after it landed (CPU), second before (GPU)."""
    rows = []
    for _ in range(runs):
        qbo_reset()
        finish()
        stamp(queries[13], TIMESTAMP)
        finish()
        qbo_resolve(queries[13], RESULT, 0)
        heavy_c()
        stamp(queries[14], TIMESTAMP)
        qbo_resolve(queries[14], RESULT, 8)
        out = qbo_read()
        rows.append((out[1] - out[0], result(queries[14]) - result(queries[13])))
    return rows


def resolve_cost(kind, iterations=200):
    """Wall time per iteration: small draw, timer query, immediate GPU-path resolve."""
    samples = []
    for _ in range(5):
        finish()
        t0 = time.monotonic_ns()
        for i in range(iterations):
            if kind == 'timestamp':
                draw(1)
                stamp(queries[15], TIMESTAMP)
            elif kind == 'elapsed':
                begin(TIME_ELAPSED, queries[11])
                draw(1)
                end(TIME_ELAPSED)
            else:
                draw(1)
                continue
            qbo_resolve(queries[15] if kind == 'timestamp' else queries[11], RESULT, 0)
        finish()
        samples.append((time.monotonic_ns() - t0) / iterations)
    print(f'overhead {kind} + GPU-path buffer resolve: median {statistics.median(samples)/1e3:.1f} us '
          f'per iteration, samples us {[round(s/1e3, 1) for s in samples]}')


def wrap_watch(seconds):
    """Loop GPU-path resolves for `seconds`; report the intervals that cross the 36-bit rollover."""
    period_ns = int((1 << 36) * 1e9 / 12e6)
    deadline = time.monotonic() + seconds
    n = crossed = bad = 0
    last_ts = None
    while time.monotonic() < deadline:
        qbo_reset()
        finish()
        stamp(queries[13], TIMESTAMP)
        begin(TIME_ELAPSED, queries[12])
        draw(64)
        end(TIME_ELAPSED)
        stamp(queries[14], TIMESTAMP)
        qbo_resolve(queries[12], RESULT, 0)
        qbo_resolve(queries[14], RESULT, 8)
        out = qbo_read()
        t0, t1, el = result(queries[13]), result(queries[14]), result(queries[12])
        n += 1
        wrapped = t1 < t0
        if wrapped:
            crossed += 1
            print(f'WRAP observed: timestamps {t0} -> {t1} ns (period {period_ns} ns), '
                  f'elapsed client {el} ns, buffer {out[0]} ns, buffer timestamp {out[1]} vs client {t1}')
        if out[0] != el or out[1] != t1 or el > 1e9:
            bad += 1
            print(f'FAIL wrap-watch sample {n}: elapsed buffer {out[0]} client {el}; '
                  f'timestamp buffer {out[1]} client {t1}')
        last_ts = t1
    print(f'wrap watch: {n} samples, {crossed} crossed the rollover, {bad} mismatches, last timestamp {last_ts} ns')
    report('wrap watch: every buffer result equals the client result', bad == 0, f'{bad} mismatches')
    if not crossed:
        print('NOTE wrap watch did not observe a rollover in this window')


def straddle(count):
    """Queue `count` back-to-back TIME_ELAPSED intervals of ~11 ms compute with
    no CPU wait in between, so the GPU timeline is tiled and a 36-bit
    rollover must fall inside one interval.  Each interval has a TIMESTAMP at
    its start, and each result is resolved into a query buffer while pending
    (GPU path).  Checks: buffer == client for every interval, every interval
    within 2x of the median, and reports the interval that wrapped."""
    el_q = (U * count)()
    ts_q = (U * (count + 1))()
    gen_queries(count, el_q)
    gen_queries(count + 1, ts_q)
    big = U()
    gen_buffers(1, c.byref(big))
    bind_buffer(QUERY_BUFFER, big.value)
    fill = (Q * count)(*([SENTINEL] * count))
    buffer_data(QUERY_BUFFER, count * 8, fill, 0x88E8)
    finish()
    t_wall = time.monotonic_ns()
    for i in range(count):
        stamp(ts_q[i], TIMESTAMP)
        begin(TIME_ELAPSED, el_q[i])
        compute(256)
        end(TIME_ELAPSED)
        query_to_buffer(el_q[i], RESULT, P(i * 8))
    stamp(ts_q[count], TIMESTAMP)
    bind_buffer(QUERY_BUFFER, 0)
    finish()
    wall = time.monotonic_ns() - t_wall
    out = (Q * count)()
    bind_buffer(COPY_READ, big.value)
    read_buffer(COPY_READ, 0, count * 8, out)
    bind_buffer(COPY_READ, 0)
    el = [result(el_q[i]) for i in range(count)]
    ts = [result(ts_q[i]) for i in range(count + 1)]
    med = statistics.median(el)
    bad_buf = [i for i in range(count) if out[i] != el[i]]
    bad_len = [i for i in range(count) if not (med / 2 <= el[i] <= med * 2)]
    wrapped = [i for i in range(count) if ts[i + 1] < ts[i]]
    print(f'straddle: {count} intervals, wall {wall/1e9:.2f} s, sum of intervals {sum(el)/1e9:.2f} s, '
          f'median {med/1e6:.3f} ms')
    for i in wrapped:
        print(f'WRAP inside interval {i}: start ts {ts[i]} ns, next start ts {ts[i+1]} ns, '
              f'elapsed client {el[i]} ns, buffer {out[i]} ns (median {med:.0f} ns)')
    for i in (bad_buf + bad_len)[:5]:
        print(f'FAIL interval {i}: client {el[i]} ns, buffer {out[i]} ns, median {med:.0f} ns')
    report('straddle: every buffer (GPU-path) result equals the client result', not bad_buf,
           f'{len(bad_buf)} mismatches')
    report('straddle: every interval within 2x of the median', not bad_len, f'{len(bad_len)} outliers')
    report('straddle: an interval contains the 36-bit rollover', len(wrapped) == 1,
           f'{len(wrapped)} wrapped intervals')
    if wrapped:
        i = wrapped[0]
        report('straddle: the wrapped interval measures like its neighbours',
               med / 2 <= el[i] <= med * 2 and out[i] == el[i], f'{el[i]} ns vs median {med:.0f} ns')


if len(sys.argv) > 2 and sys.argv[1] == '--straddle':
    straddle(int(sys.argv[2]))
elif len(sys.argv) > 2 and sys.argv[1] == '--wrap':
    wrap_watch(float(sys.argv[2]))
else:
    for name, kind, work, gpu in cases:
        check_case(name, run_case(kind, work, gpu), expect_nonzero=work is not nothing)
    rows = pair_mixed()
    report('timestamp pair, CPU-resolved then GPU-resolved: buffer delta == client delta',
           all(a == b for a, b in rows), f'buffer deltas ns {[r[0] for r in rows]}')
    raw_now = result(queries[14]) * 12e6 / 1e9
    print(f'note: current raw timestamp about {raw_now:.0f} ticks; next 36-bit rollover in about '
          f'{((1 << 36) - raw_now) / 12e6:.0f} s (assuming 12 MHz)')
    resolve_cost('none')
    resolve_cost('timestamp')
    resolve_cost('elapsed')
    check_outputs(256, 1)  # last heavy compute was 256 rounds; last draw 1 round
assert error() == 0
print(f'{len(failures)} check(s) failed' + (f': {failures}' if failures else ''))
sys.exit(1 if failures else 0)
