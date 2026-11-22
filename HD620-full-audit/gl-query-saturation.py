"""Check that Iris saturates 32-bit GL_QUERY_BUFFER query results.

ARB_query_buffer_object writes a query result into a buffer "as if"
glGetQueryObject{i,ui,i64,ui64}v had been called; only the destination
changes.  ARB_timer_query (and the Mesa frontend, queryobj.c) saturate a
result that does not fit: GL_INT -> 0x7fffffff, GL_UNSIGNED_INT ->
0xffffffff.  Before the fix Iris stored the low 32 bits on both the CPU-ready
path (store_data_imm32) and the MI_MATH GPU path (mi_mem32).

Cases (each on the GPU path -- resolve recorded before the end snapshot
lands -- and on the CPU path -- glFinish before the resolve):
  - GL_TIMESTAMP (~1e12 ns): above UINT32_MAX, both widths saturate.
  - GL_TIME_ELAPSED across a 3 s idle gap (begin; glFlush; sleep 3 s; end):
    between INT32_MAX and UINT32_MAX, so GL_INT saturates to 0x7fffffff
    while GL_UNSIGNED_INT is exact.  This separates the I32 and U32 limits.
  - GL_TIME_ELAPSED of a small draw, GL_SAMPLES_PASSED of a 1024x1024 draw,
    GL_ANY_SAMPLES_PASSED: in range, all widths exact (clamp is a no-op).
Each run also checks: 64-bit RESULT and NO_WAIT unchanged/exact, NO_WAIT
32-bit stores are the saturated value or untouched, AVAILABLE (32-bit) == 1,
and every 32-bit store leaves the adjacent 4 bytes untouched.
Exit status is nonzero when any check fails.
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



INT = bind(gl, 'glGetQueryObjectiv', None, U, U, P)
UINT = query_to_buffer_u32
UI64 = query_to_buffer
client_i32_fn = bind(gl, 'glGetQueryObjectiv', None, U, U, PI)
flush = bind(gl, 'glFlush', None)
SAMPLES_PASSED, ANY_SAMPLES_PASSED = 0x8914, 0x8C2F
I32_MAX, U32_MAX = 0x7fffffff, 0xffffffff
HI = SENTINEL >> 32


def client_i32(q):
    value = I()
    client_i32_fn(q, RESULT, c.byref(value))
    return value.value & 0xffffffff


# (offset, getter, pname, bits) -- NO_WAIT first, as in gl-timer-conversion.py.
LAYOUT = [
    (48, UI64, NO_WAIT, 64),
    (24, UINT, NO_WAIT, 32),
    (32, INT, NO_WAIT, 32),
    (0, UI64, RESULT, 64),
    (8, UINT, RESULT, 32),
    (16, INT, RESULT, 32),
    (40, INT, AVAILABLE, 32),
]


def resolve_all(q):
    bind_buffer(QUERY_BUFFER, qbo.value)
    for offset, fn, pname, _ in LAYOUT:
        fn(q, pname, P(offset))
    bind_buffer(QUERY_BUFFER, 0)


def run(kind, gpu_path):
    qbo_reset()
    finish()
    if kind == 'timestamp':
        q = queries[2]
        heavy_g()
        stamp(q, TIMESTAMP)
    elif kind == 'elapsed-3s':
        q = queries[12]
        begin(TIME_ELAPSED, q)
        flush()
        time.sleep(3.0)
        heavy_g()
        end(TIME_ELAPSED)
    elif kind == 'elapsed-small':
        q = queries[12]
        begin(TIME_ELAPSED, q)
        heavy_g()
        end(TIME_ELAPSED)
    elif kind == 'samples':
        q = queries[8]
        begin(SAMPLES_PASSED, q)
        draw(64)
        end(SAMPLES_PASSED)
    else:
        q = queries[9]
        begin(ANY_SAMPLES_PASSED, q)
        draw(64)
        end(ANY_SAMPLES_PASSED)
    if not gpu_path:
        finish()
    resolve_all(q)
    out = qbo_read()
    assert error() == 0
    return out, result(q), client_u32(q), client_i32(q)


def check(name, kind, gpu_path, value_range, runs):
    bad = []
    seen = []
    done = attempts = 0
    while done < runs and attempts < runs + 4:
        attempts += 1
        out, v64, cu32, ci32 = run(kind, gpu_path)
        if not value_range[0] <= v64 <= value_range[1]:
            print(f'NOTE {name}: client result {v64} outside the intended range {value_range}; retrying')
            continue
        done += 1
        e_u32, e_i32 = min(v64, U32_MAX), min(v64, I32_MAX)
        slot = {off: out[off // 8] for off, *_ in LAYOUT}
        lo = {off: slot[off] & 0xffffffff for off in slot}
        seen.append((v64, lo[8], lo[16]))
        errs = []
        if (cu32, ci32) != (e_u32, e_i32):
            errs.append(f'frontend client u32/i32 {cu32:#x}/{ci32:#x}, expected {e_u32:#x}/{e_i32:#x}')
        if slot[0] != v64:
            errs.append(f'u64 RESULT {slot[0]} != client {v64}')
        if slot[48] not in (SENTINEL, v64):
            errs.append(f'u64 NO_WAIT {slot[48]:#x} neither untouched nor {v64}')
        if lo[8] != e_u32:
            errs.append(f'u32 RESULT {lo[8]:#x} != {e_u32:#x}')
        if lo[16] != e_i32:
            errs.append(f'i32 RESULT {lo[16]:#x} != {e_i32:#x}')
        if slot[24] != SENTINEL and lo[24] != e_u32:
            errs.append(f'u32 NO_WAIT {lo[24]:#x} != {e_u32:#x}')
        if slot[32] != SENTINEL and lo[32] != e_i32:
            errs.append(f'i32 NO_WAIT {lo[32]:#x} != {e_i32:#x}')
        if lo[40] != 1:
            errs.append(f'i32 AVAILABLE {lo[40]:#x} != 1')
        for off in (8, 16, 24, 32, 40):
            if slot[off] >> 32 != HI:
                errs.append(f'32-bit store at {off} wrote bytes {off + 4}..{off + 7}: {slot[off]:#x}')
        if errs:
            bad.append(errs)
    ok = done == runs and not bad
    detail = f'{done}/{runs} runs in range, {len(bad)} with errors; (u64, u32, i32) {[(a, hex(b), hex(c)) for a, b, c in seen[:3]]}'
    if bad:
        detail += f'; first errors {bad[0]}'
    report(name, ok, detail)


SPAN = (I32_MAX + 1, U32_MAX)   # strictly between the two limits
cases = [
    ('timestamp > UINT32_MAX', 'timestamp', (U32_MAX + 1, 1 << 64), 5),
    ('time elapsed of 3 s idle gap, INT32_MAX < v <= UINT32_MAX', 'elapsed-3s', SPAN, 2),
    ('time elapsed of draw, in range', 'elapsed-small', (1, I32_MAX), 5),
    ('samples passed of 1024x1024 draw, in range', 'samples', (SIZE * SIZE, SIZE * SIZE), 5),
    ('any samples passed, boolean', 'any', (1, 1), 5),
]
for name, kind, rng, runs in cases:
    for gpu in (True, False):
        check(f'{name}, {"GPU" if gpu else "CPU"} path', kind, gpu, rng, runs)
assert error() == 0
print(f'{len(failures)} check(s) failed' + (f': {failures}' if failures else ''))
sys.exit(1 if failures else 0)
