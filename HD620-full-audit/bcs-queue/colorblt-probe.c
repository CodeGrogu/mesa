/* Linear 32bpp XY_COLOR_BLT on the Gen9 BCS: 4-byte aligned offsets and
 * pitches, every byte of the target checked (inside = pattern, outside
 * untouched). */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm/i915_drm.h>
static int fd;
static uint32_t create(uint64_t size, void **map)
{
    struct drm_i915_gem_create c = { .size = size };
    if (ioctl(fd, DRM_IOCTL_I915_GEM_CREATE, &c)) { perror("create"); exit(2); }
    struct drm_i915_gem_mmap_offset mo = { .handle = c.handle, .flags = I915_MMAP_OFFSET_WB };
    if (ioctl(fd, DRM_IOCTL_I915_GEM_MMAP_OFFSET, &mo)) { perror("mmap_offset"); exit(2); }
    *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mo.offset);
    return c.handle;
}
int main(void)
{
    fd = open("/dev/dri/renderD128", O_RDWR);
    const uint64_t bva = 1ull << 32, dva = bva + (1 << 20);
    void *bm, *dm; uint32_t bh = create(4096, &bm), dh = create(1 << 20, &dm);
    uint8_t *d = dm;
    static const int offs[] = { 0, 4, 8, 12, 16, 32, 60, 64, 4096 };
    static const int pitches[] = { 4, 8, 12, 100, 256, 1000, 4096, 32764 };
    static const int widths[] = { 1, 3, 16, 37 };
    unsigned bad_total = 0, cases = 0;
    for (unsigned o = 0; o < 9; o++) for (unsigned pi = 0; pi < 8; pi++) for (unsigned wi = 0; wi < 4; wi++)
    for (int h = 1; h <= 5; h += 4) {
        const int pitch = pitches[pi], w = widths[wi];
        if (w * 4 > pitch) continue;
        memset(d, 0xEE, 1 << 20);
        uint32_t *p = bm;
        *p++ = (2u << 29) | (0x50u << 22) | (3u << 20) | 5;   /* XY_COLOR_BLT, 32bpp RGBA */
        *p++ = (3u << 24) | (0xF0u << 16) | (uint32_t)pitch;
        *p++ = 0; *p++ = ((uint32_t)h << 16) | (uint32_t)w;
        *p++ = (uint32_t)(dva + offs[o]); *p++ = (uint32_t)((dva + offs[o]) >> 32);
        *p++ = 0x11223344u;
        *p++ = (0x26u << 23) | 3; *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0;
        *p++ = 0x05000000; *p++ = 0;
        struct drm_i915_gem_exec_object2 obj[2] = {
            { .handle = dh, .offset = dva, .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_WRITE },
            { .handle = bh, .offset = bva, .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS },
        };
        struct drm_i915_gem_execbuffer2 eb = { .buffers_ptr = (uintptr_t)obj, .buffer_count = 2,
            .batch_len = 4096, .flags = I915_EXEC_BLT | I915_EXEC_NO_RELOC };
        if (ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &eb)) { printf("execbuf: %s\n", strerror(errno)); return 2; }
        struct drm_i915_gem_wait wt = { .bo_handle = dh, .timeout_ns = 2000000000 }; ioctl(fd, DRM_IOCTL_I915_GEM_WAIT, &wt);
        unsigned bad = 0;
        for (int y = 0; y < h + 1; y++) for (int x = 0; x < pitch; x++) {
            const int inside = y < h && x < w * 4;
            const uint8_t want = inside ? (uint8_t)(0x11223344u >> (8 * (x & 3))) : 0xEE;
            if (d[offs[o] + y * pitch + x] != want) bad++;
        }
        cases++;
        if (bad) { bad_total++; printf("BAD off=%d pitch=%d w=%d h=%d: %u\n", offs[o], pitch, w, h, bad); }
    }
    printf("%u of %u cases wrong\n", bad_total, cases);
    return bad_total != 0;
}
