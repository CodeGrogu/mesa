/* Tiled XY_FAST_COPY_BLT on the Gen9 BCS: linear -> X/Y-tiled and back.
 * The tiled result is checked against a software model of legacy X tiling
 * (512 B x 8 rows, row-major) and legacy Y tiling (128 B x 32 rows made of
 * 16-byte columns), so this tells whether tiling code 2 is legacy Y or Yf.
 * Every byte of the tiled BO is checked, including outside the rectangle.
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

/* byte offset of (x bytes, y rows) in a tiled surface with pitch_B */
static size_t tiled_off(int tiling, size_t pitch_B, size_t x, size_t y)
{
    if (tiling == 1) {          /* X: 512 x 8 */
        size_t tile = (y / 8) * (pitch_B / 512) + x / 512;
        return tile * 4096 + (y % 8) * 512 + x % 512;
    } else {                    /* legacy Y: 128 x 32, 16 B columns */
        size_t tile = (y / 32) * (pitch_B / 128) + x / 128;
        size_t xi = x % 128;
        return tile * 4096 + (xi / 16) * 512 + (y % 32) * 16 + xi % 16;
    }
}

enum { BUF = 4 << 20 };
static uint32_t bh, sh, th, rh;
static const uint64_t bva = 1ull << 32, sva = bva + (1 << 20), tva = sva + BUF + (1 << 20),
                      rva = tva + BUF + (1 << 20);

static void blit(uint32_t dw0_tiling, uint32_t cd, uint32_t dpitch, uint32_t dx, uint32_t dy,
                 uint32_t w, uint32_t h, uint64_t dva, uint32_t sx, uint32_t sy, uint32_t spitch,
                 uint64_t srcva, void *bm, uint32_t wh)
{
    uint32_t *p = bm;
    *p++ = (2u << 29) | (0x42u << 22) | dw0_tiling | 8;
    *p++ = (cd << 24) | dpitch;
    *p++ = (dy << 16) | dx;
    *p++ = ((dy + h) << 16) | (dx + w);
    *p++ = (uint32_t)dva; *p++ = (uint32_t)(dva >> 32);
    *p++ = (sy << 16) | sx;
    *p++ = spitch;
    *p++ = (uint32_t)srcva; *p++ = (uint32_t)(srcva >> 32);
    *p++ = (0x26u << 23) | 3; *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 0;
    *p++ = 0x05000000; *p++ = 0;
    struct drm_i915_gem_exec_object2 obj[4] = {
        { .handle = sh, .offset = sva, .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS },
        { .handle = th, .offset = tva, .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_WRITE },
        { .handle = rh, .offset = rva, .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_WRITE },
        { .handle = bh, .offset = bva, .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS },
    };
    struct drm_i915_gem_execbuffer2 eb = { .buffers_ptr = (uintptr_t)obj, .buffer_count = 4,
        .batch_len = 4096, .flags = I915_EXEC_BLT | I915_EXEC_NO_RELOC };
    if (ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &eb)) { printf("execbuf failed: %s\n", strerror(errno)); exit(2); }
    struct drm_i915_gem_wait wt = { .bo_handle = wh, .timeout_ns = 2000000000 };
    ioctl(fd, DRM_IOCTL_I915_GEM_WAIT, &wt);
}

int main(void)
{
    fd = open("/dev/dri/renderD128", O_RDWR);
    void *bm, *sm, *tm, *rm;
    bh = create(4096, &bm); sh = create(BUF, &sm); th = create(BUF, &tm); rh = create(BUF, &rm);
    uint8_t *s = sm, *t = tm, *r = rm;
    for (int i = 0; i < BUF; i++) s[i] = (uint8_t)(i * 7 + (i >> 8) * 13 + 1);
    static const int depths[][2] = { { 1, 0 }, { 2, 1 }, { 4, 3 }, { 8, 4 }, { 16, 5 } };
    /* tiling, tiled pitch; Y with a non power-of-two pitch goes last */
    static const int cfg[][2] = { { 1, 512 }, { 1, 4096 }, { 2, 128 }, { 2, 512 }, { 2, 4096 }, { 2, 640 } };
    /* start byte alignment on both sides (tiled dx*bpp and linear sx*bpp),
     * width in bytes */
    static const int starts[] = { 0, 16, 48, 8, 4, 2, 1 };
    static const int widths[] = { 37, 74, 148, 296, 8, 24, 4, 2 };
    unsigned bad_total = 0, cases = 0;
    for (unsigned si = 0; si < 7; si++)
    for (unsigned ci = 0; ci < 6; ci++)
    for (unsigned di = 0; di < 5; di++)
    for (unsigned wi = 0; wi < 8; wi++) {
        const int tiling = cfg[ci][0], tp = cfg[ci][1];
        const int bpp = depths[di][0], cd = depths[di][1];
        if (starts[si] % bpp || widths[wi] % bpp) continue;
        const int dx = starts[si] / bpp, sx = starts[si] / bpp, w = widths[wi] / bpp, h = 29, dy = 5;
        if ((dx + w) * bpp > tp) continue;
        const int lp = 4096;
        memset(t, 0xEE, BUF); memset(r, 0xDD, BUF);
        blit((uint32_t)tiling << 13, cd, tp / 4, dx, dy, w, h, tva, sx, 1, lp, sva, bm, th);
        unsigned bad = 0;
        const size_t rows = ((dy + h + 31) / 32 + 1) * 32;
        for (size_t y = 0; y < rows; y++)
            for (size_t x = 0; x < (size_t)tp; x++) {
                const int inside = y >= (size_t)dy && y < (size_t)(dy + h) &&
                                   x >= (size_t)dx * bpp && x < (size_t)(dx + w) * bpp;
                const uint8_t want = inside ?
                    s[(y - dy + 1) * lp + (x - dx * bpp) + sx * bpp] : 0xEE;
                if (t[tiled_off(tiling, tp, x, y)] != want) bad++;
            }
        /* back to linear at the same start alignment */
        blit((uint32_t)tiling << 20, cd, lp, sx, 0, w, h, rva, dx, dy, tp / 4, tva, bm, rh);
        unsigned bad2 = 0;
        for (int y = 0; y < h + 1; y++)
            for (int x = 0; x < lp; x++) {
                const int inside = y < h && x >= sx * bpp && x < (sx + w) * bpp;
                const uint8_t want = inside ? s[(size_t)(y + 1) * lp + x] : 0xDD;
                if (r[(size_t)y * lp + x] != want) bad2++;
            }
        cases++;
        printf("RES tiling=%c pitch=%4d bpp=%2d start=%3d width=%3d to_tiled=%s from_tiled=%s\n",
               tiling == 1 ? 'X' : 'Y', tp, bpp, starts[si], widths[wi], bad ? "BAD" : "ok", bad2 ? "BAD" : "ok");
        fflush(stdout);
        if (bad || bad2) bad_total++;
    }
    printf("%u of %u cases wrong\n", bad_total, cases);
    return bad_total != 0;
}
