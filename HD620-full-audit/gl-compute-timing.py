"""Compare Iris OpenGL GPU timers with checked compute output and fence wall time."""
import ctypes as c
import os
import statistics
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
if 'EGL_TEST_LIBRARY' in os.environ:
    with open('/proc/self/maps') as maps:
        paths = {line.split()[-1] for line in maps if '/libEGL.so.' in line or '/libgallium-' in line}
    for path in sorted(paths):
        print(f'Mapped driver library {path}')

source = c.c_char_p(b'''
#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Output { uint values[]; };
uniform uint rounds;
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint value = i;
    for (uint j = 0; j < rounds; ++j)
        value = (value * 1664525u + 1013904223u) ^ (value >> 7);
    values[i] = value;
}
''')
shader = bind(gl, 'glCreateShader', U, U)(0x91B9)
bind(gl, 'glShaderSource', None, U, I, c.POINTER(c.c_char_p), PI)(shader, 1, c.byref(source), None)
bind(gl, 'glCompileShader', None, U)(shader)
status = I()
bind(gl, 'glGetShaderiv', None, U, U, PI)(shader, 0x8B81, c.byref(status))
assert status.value
program = bind(gl, 'glCreateProgram', U)()
bind(gl, 'glAttachShader', None, U, U)(program, shader)
bind(gl, 'glLinkProgram', None, U)(program)
bind(gl, 'glGetProgramiv', None, U, U, PI)(program, 0x8B82, c.byref(status))
assert status.value
bind(gl, 'glUseProgram', None, U)(program)
location = bind(gl, 'glGetUniformLocation', I, U, c.c_char_p)(program, b'rounds')
assert location >= 0

num = 1 << 20
buffer = U()
queries = (U * 3)()
output = (U * num)()
bind(gl, 'glGenBuffers', None, I, PU)(1, c.byref(buffer))
bind(gl, 'glBindBufferBase', None, U, U, U)(0x90D2, 0, buffer.value)
bind(gl, 'glBufferData', None, U, Z, P, U)(0x90D2, c.sizeof(output), None, 0x88E8)
bind(gl, 'glGenQueries', None, I, PU)(3, queries)
uniform = bind(gl, 'glUniform1ui', None, I, U)
stamp = bind(gl, 'glQueryCounter', None, U, U)
query = bind(gl, 'glGetQueryObjectui64v', None, U, U, c.POINTER(Q))
dispatch = bind(gl, 'glDispatchCompute', None, U, U, U)
barrier = bind(gl, 'glMemoryBarrier', None, U)
finish = bind(gl, 'glFinish', None)
error = bind(gl, 'glGetError', U)
begin = bind(gl, 'glBeginQuery', None, U, U)
end = bind(gl, 'glEndQuery', None, U)
read_buffer = bind(gl, 'glGetBufferSubData', None, U, Z, Z, P)
for rounds in (16, 256):
    uniform(location, rounds)
    for mode in ('timestamp pair', 'time elapsed', 'timestamp pair with explicit drains'):
        stamps, walls = [], []
        for trial in range(10):
            first, last = Q(), Q()
            t0 = time.monotonic_ns()
            if mode == 'time elapsed':
                begin(0x88BF, queries[2])  # GL_TIME_ELAPSED
            else:
                stamp(queries[0], 0x8E28)  # GL_TIMESTAMP
                if mode.endswith('explicit drains'):
                    finish()
            dispatch(num // 64, 1, 1)
            barrier(0x200)  # GL_BUFFER_UPDATE_BARRIER_BIT
            if mode == 'time elapsed':
                end(0x88BF)
            else:
                if mode.endswith('explicit drains'):
                    finish()
                stamp(queries[1], 0x8E28)
            finish()
            wall = time.monotonic_ns() - t0
            if mode == 'time elapsed':
                query(queries[2], 0x8866, c.byref(last))
            else:
                query(queries[0], 0x8866, c.byref(first))
                query(queries[1], 0x8866, c.byref(last))
            assert error() == 0
            if trial:
                stamps.append(last.value - first.value)
                walls.append(wall)
        read_buffer(0x90D2, 0, c.sizeof(output), output)
        for i in range(0, num, 1021):
            value = i
            for _ in range(rounds):
                value = ((value * 1664525 + 1013904223) & 0xffffffff) ^ (value >> 7)
            assert output[i] == value
        assert error() == 0
        print(f'{num} work-items, {rounds} rounds, {mode}, output checks passed')
        for name, samples in [('GL query difference', stamps), ('wall time through finish', walls)]:
            print(f'  {name} median {statistics.median(samples)/1e6:.6f} ms, samples ns {samples}')
