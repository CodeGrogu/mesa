"""Export a Gen9 VA surface and import its exact DMA-BUF planes into Iris EGL."""
import ctypes as c
import os

os.environ['EGL_PLATFORM'] = 'surfaceless'
os.environ['LIBVA_DRIVER_NAME'] = 'iHD'
va, drm, egl = [c.CDLL(lib) for lib in ('libva.so.2', 'libva-drm.so.2', 'libEGL.so.1')]
P, I, U, Q = c.c_void_p, c.c_int, c.c_uint32, c.c_uint64

def bind(lib, name, returns, *args):
    f = getattr(lib, name)
    f.restype, f.argtypes = returns, args
    return f

def va_check(rc):
    if rc:
        raise RuntimeError(f'VA-API status {rc}')

class GenericValue(c.Structure):
    class Value(c.Union):
        _fields_ = [('i', I), ('p', P)]
    _fields_ = [('kind', I), ('value', Value)]

class SurfaceAttrib(c.Structure):
    _fields_ = [('kind', I), ('flags', U), ('v', GenericValue)]

class PrimeObject(c.Structure):
    _fields_ = [('fd', I), ('size', U), ('modifier', Q)]

class PrimeLayer(c.Structure):
    _fields_ = [('format', U), ('planes', U), ('objects', U * 4), ('offsets', U * 4), ('pitches', U * 4)]

class PrimeSurface(c.Structure):
    _fields_ = [('format', U), ('width', U), ('height', U), ('num_objects', U),
                ('objects', PrimeObject * 4), ('num_layers', U), ('layers', PrimeLayer * 4)]

device = os.open('/dev/dri/renderD128', os.O_RDWR | os.O_CLOEXEC)
vd = bind(drm, 'vaGetDisplayDRM', P, I)(device)
assert vd
major, minor = I(), I()
va_check(bind(va, 'vaInitialize', I, P, P, P)(vd, c.byref(major), c.byref(minor)))
print(bind(va, 'vaQueryVendorString', c.c_char_p, P)(vd).decode())
ed = bind(egl, 'eglGetDisplay', P, P)(None)
assert ed and bind(egl, 'eglInitialize', U, P, P, P)(ed, None, None)
extensions = bind(egl, 'eglQueryString', c.c_char_p, P, I)(ed, 0x3055).decode().split()
assert 'EGL_EXT_image_dma_buf_import_modifiers' in extensions
proc = bind(egl, 'eglGetProcAddress', P, c.c_char_p)
create_image = c.CFUNCTYPE(P, P, P, U, P, P)(proc(b'eglCreateImageKHR'))
destroy_image = c.CFUNCTYPE(U, P, P)(proc(b'eglDestroyImageKHR'))
create_surface = bind(va, 'vaCreateSurfaces', I, P, U, U, U, P, U, P, U)
destroy_surface = bind(va, 'vaDestroySurfaces', I, P, P, I)
export_surface = bind(va, 'vaExportSurfaceHandle', I, P, U, U, U, P)
error = bind(egl, 'eglGetError', I)

for label, rt_format in [('NV12', 1), ('P010', 0x100)]:
    fourcc = U(int.from_bytes(label.encode(), 'little'))
    attr = SurfaceAttrib(1, 2, GenericValue(1, GenericValue.Value(i=fourcc.value)))
    surface = U()
    va_check(create_surface(vd, rt_format, 640, 360, c.byref(surface), 1, c.byref(attr), 1))
    exported = PrimeSurface()
    try:
        va_check(export_surface(vd, surface, 0x40000000, 0x0001 | 0x0008, c.byref(exported)))
        assert exported.num_layers == 1 and 1 <= exported.num_objects <= 4
        layer = exported.layers[0]
        assert layer.format == fourcc.value and layer.planes == 2
        attrs = [0x3057, exported.width, 0x3056, exported.height, 0x3271, layer.format]
        for plane in range(layer.planes):
            obj = exported.objects[layer.objects[plane]]
            attrs += [0x3272 + 3*plane, obj.fd, 0x3273 + 3*plane, layer.offsets[plane],
                      0x3274 + 3*plane, layer.pitches[plane], 0x3443 + 2*plane, obj.modifier & 0xffffffff,
                      0x3444 + 2*plane, obj.modifier >> 32]
        attrs.append(0x3038)
        image = create_image(ed, None, 0x3270, None, (I * len(attrs))(*attrs))
        modifiers = [hex(exported.objects[i].modifier) for i in range(exported.num_objects)]
        if not image:
            raise RuntimeError(f'{label} modifiers {modifiers}: EGL error 0x{error():x}')
        assert destroy_image(ed, image)
        print(f'{label} {exported.width}x{exported.height}, {layer.planes} planes, {modifiers} imported')
    finally:
        for i in range(exported.num_objects):
            os.close(exported.objects[i].fd)
        va_check(destroy_surface(vd, c.byref(surface), 1))
bind(egl, 'eglTerminate', U, P)(ed)
va_check(bind(va, 'vaTerminate', I, P)(vd))
os.close(device)
