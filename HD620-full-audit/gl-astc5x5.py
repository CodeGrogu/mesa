"""Native ASTC 5x5 next to HiZ and CCS data in Iris on Gfx9 (Mesa #1892).

usage: gl-astc5x5.py ASTC_FILE ITERATIONS OUT.npy-like-raw-file

Renders a depth texture (HiZ) and an RGBA8 colour texture (CCS-capable),
then samples them together with an ASTC 5x5 texture:

  astc     one draw sampling only the ASTC texture
  both     one draw sampling ASTC, depth and colour together
  alt      alternating draws: ASTC only, then depth + colour, in one batch
  compute  one dispatch sampling ASTC and depth

Each iteration submits every case.  The final images are written to OUT as
raw RGBA8 (4 cases x 128 x 128), so a native run can be compared with an
IRIS_GFX9_EMULATE_ASTC_5X5=true run.  A GPU hang shows up as a GL reset,
a timeout or garbage; check dmesg as well.
"""
import ctypes as c
import os
import struct
import sys
import time

os.environ['EGL_PLATFORM'] = 'surfaceless'
egl = c.CDLL(os.environ.get('EGL_TEST_LIBRARY', 'libEGL.so.1'))
P, U, I, F = c.c_void_p, c.c_uint, c.c_int, c.c_float
PI, PU = c.POINTER(I), c.POINTER(U)
gpa = egl.eglGetProcAddress
gpa.restype, gpa.argtypes = P, (c.c_char_p,)


def egl_fn(name, ret, *args):
    f = getattr(egl, name)
    f.restype, f.argtypes = ret, args
    return f


def gl_fn(name, ret, *args):
    address = gpa(name.encode())
    if not address:
        raise RuntimeError(f'no {name}')
    return c.CFUNCTYPE(ret, *args)(address)


display = egl_fn('eglGetDisplay', P, P)(None)
assert display and egl_fn('eglInitialize', U, P, PI, PI)(display, None, None)
assert egl_fn('eglBindAPI', U, U)(0x30A2)
config, n = P(), I()
attrs = (I * 5)(0x3040, 8, 0x3033, 1, 0x3038)
assert egl_fn('eglChooseConfig', U, P, PI, c.POINTER(P), I, PI)(display, attrs, c.byref(config), 1, c.byref(n))
attrs = (I * 7)(0x3098, 4, 0x30FB, 5, 0x30FD, 1, 0x3038)
ctx = egl_fn('eglCreateContext', P, P, P, P, PI)(display, config, None, attrs)
assert ctx and egl_fn('eglMakeCurrent', U, P, P, P, P)(display, None, None, ctx)
glGetString = gl_fn('glGetString', c.c_char_p, U)
print(glGetString(0x1F01).decode(), glGetString(0x1F02).decode())
with open('/proc/self/maps') as maps:
    for path in sorted({l.split()[-1] for l in maps if '/libgallium-' in l}):
        print('Mapped', path)

glGenTextures = gl_fn('glGenTextures', None, I, PU)
glBindTexture = gl_fn('glBindTexture', None, U, U)
glTexStorage2D = gl_fn('glTexStorage2D', None, U, I, U, I, I)
glCompressedTexSubImage2D = gl_fn('glCompressedTexSubImage2D', None, U, I, I, I, I, I, U, I, P)
glTexParameteri = gl_fn('glTexParameteri', None, U, U, I)
glGenFramebuffers = gl_fn('glGenFramebuffers', None, I, PU)
glBindFramebuffer = gl_fn('glBindFramebuffer', None, U, U)
glFramebufferTexture2D = gl_fn('glFramebufferTexture2D', None, U, U, U, U, I)
glCheckFramebufferStatus = gl_fn('glCheckFramebufferStatus', U, U)
glViewport = gl_fn('glViewport', None, I, I, I, I)
glClearColor = gl_fn('glClearColor', None, F, F, F, F)
glClearDepthf = gl_fn('glClearDepthf', None, F)
glClear = gl_fn('glClear', None, U)
glEnable = gl_fn('glEnable', None, U)
glDisable = gl_fn('glDisable', None, U)
glDepthFunc = gl_fn('glDepthFunc', None, U)
glDrawArrays = gl_fn('glDrawArrays', None, U, I, I)
glGenVertexArrays = gl_fn('glGenVertexArrays', None, I, PU)
glBindVertexArray = gl_fn('glBindVertexArray', None, U)
glActiveTexture = gl_fn('glActiveTexture', None, U)
glReadPixels = gl_fn('glReadPixels', None, I, I, I, I, U, U, P)
glFinish = gl_fn('glFinish', None)
glFlush = gl_fn('glFlush', None)
glGetError = gl_fn('glGetError', U)
glGetGraphicsResetStatus = gl_fn('glGetGraphicsResetStatus', U)
glDispatchCompute = gl_fn('glDispatchCompute', None, U, U, U)
glBindImageTexture = gl_fn('glBindImageTexture', None, U, U, I, c.c_ubyte, I, U, U)
glMemoryBarrier = gl_fn('glMemoryBarrier', None, U)
glCreateShader = gl_fn('glCreateShader', U, U)
glShaderSource = gl_fn('glShaderSource', None, U, I, c.POINTER(c.c_char_p), PI)
glCompileShader = gl_fn('glCompileShader', None, U)
glGetShaderiv = gl_fn('glGetShaderiv', None, U, U, PI)
glGetShaderInfoLog = gl_fn('glGetShaderInfoLog', None, U, I, PI, c.c_char_p)
glCreateProgram = gl_fn('glCreateProgram', U)
glAttachShader = gl_fn('glAttachShader', None, U, U)
glLinkProgram = gl_fn('glLinkProgram', None, U)
glGetProgramiv = gl_fn('glGetProgramiv', None, U, U, PI)
glUseProgram = gl_fn('glUseProgram', None, U)
glGetUniformLocation = gl_fn('glGetUniformLocation', I, U, c.c_char_p)
glUniform1i = gl_fn('glUniform1i', None, I, I)

TEX2D, FB = 0x0DE1, 0x8D40
COLOR0, DEPTH = 0x8CE0, 0x8D00


def tex(fmt, w, h):
    t = U()
    glGenTextures(1, c.byref(t))
    glBindTexture(TEX2D, t.value)
    glTexStorage2D(TEX2D, 1, fmt, w, h)
    glTexParameteri(TEX2D, 0x2801, 0x2600)  # MIN_FILTER NEAREST
    glTexParameteri(TEX2D, 0x2800, 0x2600)  # MAG_FILTER NEAREST
    return t.value


def fbo(color, depth=None):
    f = U()
    glGenFramebuffers(1, c.byref(f))
    glBindFramebuffer(FB, f.value)
    if color:
        glFramebufferTexture2D(FB, COLOR0, TEX2D, color, 0)
    if depth:
        glFramebufferTexture2D(FB, DEPTH, TEX2D, depth, 0)
    assert glCheckFramebufferStatus(FB) == 0x8CD5
    return f.value


def program(*stages):
    prog = glCreateProgram()
    for kind, text in stages:
        s = glCreateShader(kind)
        src = c.c_char_p(text.encode())
        glShaderSource(s, 1, c.byref(src), None)
        glCompileShader(s)
        ok = I()
        glGetShaderiv(s, 0x8B81, c.byref(ok))
        if not ok.value:
            log = c.create_string_buffer(4096)
            glGetShaderInfoLog(s, 4096, None, log)
            raise RuntimeError(log.value.decode())
        glAttachShader(prog, s)
    glLinkProgram(prog)
    ok = I()
    glGetProgramiv(prog, 0x8B82, c.byref(ok))
    assert ok.value
    glUseProgram(prog)
    for i, name in enumerate((b'astc', b'depth', b'color')):
        loc = glGetUniformLocation(prog, name)
        if loc >= 0:
            glUniform1i(loc, i)
    return prog


VS = '''#version 450
out vec2 uv;
void main() {
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    uv = p * 0.5 + 0.5;
    /* Depth varies across the target, so the depth texture holds a gradient. */
    gl_Position = vec4(p, p.x * 0.4 + p.y * 0.3, 1.0);
}
'''
FS_HEAD = '''#version 450
in vec2 uv;
out vec4 o;
layout(binding = 0) uniform sampler2D astc;
layout(binding = 1) uniform sampler2D depth;
layout(binding = 2) uniform sampler2D color;
'''

astc_path, iterations, out_path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
data = open(astc_path, 'rb').read()
magic, bx, by, bz = struct.unpack('<IBBB', data[:7])
assert magic == 0x5CA1AB13 and (bx, by, bz) == (5, 5, 1), 'need a 2D 5x5 .astc file'
w = int.from_bytes(data[7:10], 'little')
h = int.from_bytes(data[10:13], 'little')
blocks = data[16:]
print(f'ASTC 5x5 {w}x{h}, {len(blocks)} bytes')

vao = U()
glGenVertexArrays(1, c.byref(vao))
glBindVertexArray(vao.value)

astc_tex = tex(0x93B2, w, h)                           # COMPRESSED_RGBA_ASTC_5x5_KHR
buf = c.create_string_buffer(blocks, len(blocks))
glCompressedTexSubImage2D(TEX2D, 0, 0, 0, w, h, 0x93B2, len(blocks), buf)
S = 128
depth_tex = tex(0x8CAC, S, S)                          # DEPTH_COMPONENT32F, HiZ
color_tex = tex(0x8058, S, S)                          # RGBA8, CCS-capable
outs = [tex(0x8058, S, S) for _ in range(4)]
out_fbos = [fbo(t) for t in outs]
scratch = tex(0x8058, S, S)
depth_fbo = fbo(scratch, depth_tex)
color_fbo = fbo(color_tex)

draw_depth = program((0x8B31, VS), (0x8B30, FS_HEAD + 'void main() { o = vec4(1); }'))
draw_color = program((0x8B31, VS), (0x8B30, FS_HEAD +
                     'void main() { o = vec4(uv, 1.0 - uv.x, 1.0); }'))
p_astc = program((0x8B31, VS), (0x8B30, FS_HEAD +
                 'void main() { o = texture(astc, uv); }'))
p_both = program((0x8B31, VS), (0x8B30, FS_HEAD + '''void main() {
    vec4 a = texture(astc, uv);
    o = vec4(a.r, texture(depth, uv).r, texture(color, uv).b, a.a);
}'''))
p_aux = program((0x8B31, VS), (0x8B30, FS_HEAD +
                'void main() { o = vec4(texture(depth, uv).r, texture(color, uv).gb, 1.0); }'))
p_cs = program((0x91B9, '''#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 0) uniform sampler2D astc;
layout(binding = 1) uniform sampler2D depth;
layout(rgba8, binding = 0) writeonly uniform image2D dst;
void main() {
    vec2 uv = (vec2(gl_GlobalInvocationID.xy) + 0.5) / 128.0;
    vec4 a = texture(astc, uv);
    imageStore(dst, ivec2(gl_GlobalInvocationID.xy), vec4(a.rg, texture(depth, uv).r, a.a));
}'''))


def bind_textures():
    for unit, t in enumerate((astc_tex, depth_tex, color_tex)):
        glActiveTexture(0x84C0 + unit)
        glBindTexture(TEX2D, t)


def make_aux_data():
    """Rewrite depth and colour so they are HiZ/CCS-compressed again."""
    glBindFramebuffer(FB, depth_fbo)
    glViewport(0, 0, S, S)
    glEnable(0x0B71)                                   # DEPTH_TEST
    glDepthFunc(0x0207)                                # ALWAYS
    glClearDepthf(1.0)
    glClear(0x100 | 0x4000)
    glUseProgram(draw_depth)
    glDrawArrays(0x0004, 0, 3)
    glDisable(0x0B71)
    glBindFramebuffer(FB, color_fbo)
    glUseProgram(draw_color)
    glDrawArrays(0x0004, 0, 3)


def full_draw(prog, target):
    glBindFramebuffer(FB, out_fbos[target])
    glViewport(0, 0, S, S)
    glUseProgram(prog)
    bind_textures()
    glDrawArrays(0x0004, 0, 3)


t0 = time.time()
for it in range(iterations):
    make_aux_data()
    full_draw(p_astc, 0)
    make_aux_data()
    full_draw(p_both, 1)
    for _ in range(4):
        make_aux_data()
        full_draw(p_astc, 2)
        full_draw(p_aux, 2)
    make_aux_data()
    glUseProgram(p_cs)
    bind_textures()
    glBindImageTexture(0, outs[3], 0, 0, 0, 0x88B9, 0x8058)  # WRITE_ONLY, RGBA8
    glDispatchCompute(S // 8, S // 8, 1)
    glMemoryBarrier(0xFFFFFFFF)
    if it % 10 == 9:
        glFinish()
        status = glGetGraphicsResetStatus()
        if status:
            print(f'GL reset status 0x{status:x} after iteration {it}')
            sys.exit(1)
glFinish()
err, status = glGetError(), glGetGraphicsResetStatus()
print(f'{iterations} iterations in {time.time() - t0:.2f} s, GL error 0x{err:x}, reset 0x{status:x}')

# The alt target last got the aux-only draw; redo the ASTC-only half so the
# image compares ASTC sampled right after aux data.
make_aux_data()
full_draw(p_aux, 2)
full_draw(p_astc, 2)
glFinish()
with open(out_path, 'wb') as f:
    for target in range(4):
        glBindFramebuffer(FB, out_fbos[target])
        px = c.create_string_buffer(S * S * 4)
        glReadPixels(0, 0, S, S, 0x1908, 0x1401, px)
        f.write(px.raw)
sys.exit(1 if err or status else 0)
