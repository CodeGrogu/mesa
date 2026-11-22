/* Linear XY_SRC_COPY_BLT constraints on Gen9 BCS: for a range of source and
 * destination byte offsets, pitches and depths, copy a W x H rectangle and
 * check every byte (and that nothing outside it was written).
 */
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

enum { BUF = 4 << 20 };
int main(void)
{
    fd = open("/dev/dri/renderD128", O_RDWR);
    const uint64_t bva = 1ull << 32, sva = bva + (1 << 20), dva = sva + BUF + (1 << 20);
    void *bm, *sm, *dm;
    uint32_t bh = create(4096, &bm), sh = create(BUF, &sm), dh = create(BUF, &dm);
    uint8_t *s = sm, *d = dm;
    for (int i = 0; i < BUF; i++) s[i] = (uint8_t)(i * 7 + (i >> 8) * 13 + 1);
    static const int depths[][2] = { { 1, 0 }, { 2, 1 }, { 4, 3 } };
    static const int offs[] = { 0, 1, 4, 8, 16, 32, 64, 128, 4096 };
    static const int pitches[] = { 7, 64, 100, 128, 1000, 4096, 32764, 32767 };
    unsigned bad_total = 0, cases = 0;
    for (unsigned di = 0; di < 3; di++) {
        const int bpp = depths[di][0], cd = depths[di][1];
        for (unsigned so = 0; so < sizeof(offs) / sizeof(offs[0]); so++)
        for (unsigned doi = 0; doi < sizeof(offs) / sizeof(offs[0]); doi++)
        for (unsigned pi = 0; pi < sizeof(pitches) / sizeof(pitches[0]); pi++) {
            const int pitch = pitches[pi];
            const int w = (pitch / bpp) < 37 ? pitch / bpp : 37, h = 5;
            if (w < 1) continue;
            memset(d, 0xEE, BUF);
            uint32_t *p = bm;
            *p++ = (2u << 29) | (0x53u << 22) | (3u << 20) | 8; /* XY_SRC_COPY_BLT, write RGBA */
            *p++ = ((uint32_t)cd << 24) | (0xCCu << 16) | (uint32_t)pitch; /* SRCCOPY rop */
            *p++ = 0;                                         /* dst y1 x1 */
            *p++ = ((uint32_t)h << 16) | (uint32_t)w;         /* dst y2 x2 */
            *p++ = (uint32_t)(dva + offs[doi]); *p++ = (uint32_t)((dva + offs[doi]) >> 32);
            *p++ = 0;                                         /* src y1 x1 */
            *p++ = (uint32_t)pitch;                           /* src pitch */
            *p++ = (uint32_t)(sva + offs[so]); *p++ = (uint32_t)((sva + offs[so]) >> 32);
            *p++ = (0x26u << 23) | 3; *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0; /* MI_FLUSH_DW */
            *p++ = 0x05000000; *p++ = 0;
            struct drm_i915_gem_exec_object2 obj[3] = {
                { .handle = sh, .offset = sva, .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS },
                { .handle = dh, .offset = dva, .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_WRITE },
                { .handle = bh, .offset = bva, .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS },
            };
            struct drm_i915_gem_execbuffer2 eb = { .buffers_ptr = (uintptr_t)obj, .buffer_count = 3,
                .batch_len = 4096, .flags = I915_EXEC_BLT | I915_EXEC_NO_RELOC };
            if (ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &eb)) { printf("execbuf failed: %s\n", strerror(errno)); return 2; }
            struct drm_i915_gem_wait wt = { .bo_handle = dh, .timeout_ns = 2000000000 };
            ioctl(fd, DRM_IOCTL_I915_GEM_WAIT, &wt);
            unsigned bad = 0;
            for (int y = 0; y < h + 1; y++)
                for (int x = 0; x < pitch; x++) {
                    const size_t di_ = offs[doi] + (size_t)y * pitch + x;
                    const int inside = y < h && x < w * bpp;
                    const uint8_t want = inside ? s[offs[so] + (size_t)y * pitch + x] : 0xEE;
                    if (d[di_] != want) bad++;
                }
            cases++;
            printf("RES bpp=%d so=%d do=%d pitch=%d %s\n", bpp, offs[so], offs[doi], pitch, bad ? "BAD" : "ok");
            if (bad) {
                bad_total++;
                if (0)
                    printf("BAD bpp=%2d src_off=%4d dst_off=%4d pitch=%5d: %u bytes wrong\n",
                           bpp, offs[so], offs[doi], pitch, bad);
            }
        }
    }
    printf("%u of %u cases wrong\n", bad_total, cases);
    return bad_total != 0;
}
