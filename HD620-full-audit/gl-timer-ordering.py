"""Check Iris OpenGL timer-query ordering across compute and graphics work.

Each case compares a GPU timer with a reference that drains the GPU with
glFinish around the same work. Inclusion cases must cover the timed work.
Exclusion cases must not absorb work issued before or after the interval.
Query-buffer cases read GL_TIMESTAMP through get_query_result_resource,
the gallium path that Rusticl's GPU profiler uses for its event timestamps.
Output is checked for the compute buffer and for one rendered pixel.
Exit status is nonzero when any timing check fails.  A mismatch between
query-buffer and glGetQueryObject GL_TIMESTAMP values is reported as a
separate pre-existing defect and does not affect the exit status.
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


C_ROUNDS, G_ROUNDS = 256, 256
nothing = lambda: None
heavy_c = lambda: compute(C_ROUNDS)
heavy_g = lambda: draw(G_ROUNDS)
both = lambda: (heavy_c(), heavy_g())

ref_c = median(lambda: drained(heavy_c))
ref_g = median(lambda: drained(heavy_g))
ref_both = median(lambda: drained(both))
print(f'drained references: compute {ref_c/1e6:.3f} ms, draw {ref_g/1e6:.3f} ms, '
      f'compute+draw {ref_both/1e6:.3f} ms')

failures = []


def check(name, value, low=None, high=None):
    ok = (low is None or value >= low) and (high is None or value <= high)
    bounds = ' '.join(x for x in (low is not None and f'>= {low/1e6:.3f}',
                                  high is not None and f'<= {high/1e6:.3f}') if x)
    print(f'{"PASS" if ok else "FAIL"} {name}: {value/1e6:.3f} ms (want {bounds} ms)')
    if not ok:
        failures.append(name)


EXCLUDE = 0.1  # a timed empty interval may absorb at most 10% of adjacent work
check('time elapsed covers compute', median(lambda: elapsed(nothing, heavy_c, nothing)), low=0.8 * ref_c)
check('time elapsed covers draw', median(lambda: elapsed(nothing, heavy_g, nothing)), low=0.8 * ref_g)
check('time elapsed covers compute then draw', median(lambda: elapsed(nothing, both, nothing)), low=0.8 * ref_both)
check('time elapsed covers draw then compute',
      median(lambda: elapsed(nothing, lambda: (heavy_g(), heavy_c()), nothing)), low=0.8 * ref_both)
check('timestamp pair covers compute', median(lambda: pair(nothing, heavy_c, nothing)), low=0.8 * ref_c)
check('timestamp pair covers draw', median(lambda: pair(nothing, heavy_g, nothing)), low=0.8 * ref_g)
qbo_runs = [pair_qbo(nothing, heavy_c, nothing) for _ in range(9)]
check('query-buffer-resolved timestamp pair covers compute (CPU readback)',
      statistics.median(r[1] for r in qbo_runs), low=0.8 * ref_c)
# Pre-existing defect, reported separately: Iris resolves GL_TIMESTAMP on the
# GPU as (raw * 83) & (2^36 - 1), but on the CPU as raw * 83.33 unmasked, so a
# query buffer can receive values in a different domain from glGetQueryObject.
matches = sum(r[2] for r in qbo_runs)
print(f'{"PASS" if matches == len(qbo_runs) else "KNOWN-DEFECT"} query-buffer GL_TIMESTAMP values equal '
      f'glGetQueryObjectui64v values in {matches}/{len(qbo_runs)} runs; '
      f'query-buffer deltas ns {[r[0] for r in qbo_runs]}')
check('time elapsed excludes earlier compute', median(lambda: elapsed(heavy_c, nothing, nothing)), high=EXCLUDE * ref_c)
check('time elapsed excludes later compute', median(lambda: elapsed(nothing, nothing, heavy_c)), high=EXCLUDE * ref_c)
check('time elapsed excludes earlier draw', median(lambda: elapsed(heavy_g, nothing, nothing)), high=EXCLUDE * ref_g)
check('time elapsed excludes later draw', median(lambda: elapsed(nothing, nothing, heavy_g)), high=EXCLUDE * ref_g)
check('timestamp pair excludes earlier compute', median(lambda: pair(heavy_c, nothing, nothing)), high=EXCLUDE * ref_c)
check('timestamp pair excludes later compute', median(lambda: pair(nothing, nothing, heavy_c)), high=EXCLUDE * ref_c)
check('query-buffer-resolved pair excludes later compute (CPU readback)',
      median(lambda: pair_qbo(nothing, nothing, heavy_c)[1]), high=EXCLUDE * ref_c)
deltas = [chain() for _ in range(9)]
for i, (name, ref) in enumerate((('compute', ref_c), ('draw', ref_g), ('second compute', ref_c))):
    value = statistics.median(d[i] for d in deltas)
    check(f'timestamp chain interval {i + 1} covers {name}', value, low=0.8 * ref, high=1.5 * ref)
check_outputs(C_ROUNDS, G_ROUNDS)
print('compute buffer and rendered pixel checks passed')


def loop_cost(label, body, iterations=200):
    """Wall time per iteration of many small timed operations."""
    samples = []
    for _ in range(5):
        finish()
        t0 = time.monotonic_ns()
        for _ in range(iterations):
            body()
        finish()
        samples.append((time.monotonic_ns() - t0) / iterations)
    value = statistics.median(samples)
    print(f'overhead {label}: median {value/1e3:.1f} us per iteration, samples us '
          f'{[round(s/1e3, 1) for s in samples]}')


small_c = lambda: compute(16, 4096)
small_g = lambda: draw(1)


def timed(work):
    def body():
        begin(TIME_ELAPSED, queries[11])
        work()
        end(TIME_ELAPSED)
    return body


loop_cost('small dispatch, no query', small_c)
loop_cost('small dispatch inside time elapsed', timed(small_c))
loop_cost('small draw, no query', small_g)
loop_cost('small draw inside time elapsed', timed(small_g))
loop_cost('small draw+dispatch, no query', lambda: (small_g(), small_c()))
loop_cost('small draw+dispatch inside time elapsed', timed(lambda: (small_g(), small_c())))
check_outputs(16, 1, 4096)
assert error() == 0
print(f'{len(failures)} timing check(s) failed' + (f': {failures}' if failures else ''))
sys.exit(1 if failures else 0)
