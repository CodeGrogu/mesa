/* Does the i915 command parser accept, on the Gen9 copy engine:
 *   1. MI_SEMAPHORE_WAIT (polling, already satisfied)?
 *   2. MI_BATCH_BUFFER_START (2nd level) to another BO?
 *   3. MI_BATCH_BUFFER_START chaining (1st level) to another BO?
 *   4. MI_STORE_DATA_IMM + MI_FLUSH_DW post-sync timestamp?
 * Prints the execbuf result and whether the marker writes landed.
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
    memset(*map, 0, size);
    return c.handle;
}
static uint32_t *sdi(uint32_t *p, uint64_t a, uint32_t v)
{ *p++ = (0x20u << 23) | 2; *p++ = (uint32_t)a; *p++ = (uint32_t)(a >> 32); *p++ = v; return p; }

int main(void)
{
    fd = open("/dev/dri/renderD128", O_RDWR);
    const uint64_t bva = 1ull << 32, b2va = bva + (1 << 20), dva = bva + (2 << 20);
    void *bm, *b2m, *dm;
    uint32_t bh = create(4096, &bm), b2h = create(4096, &b2m), dh = create(4096, &dm);
    volatile uint32_t *d = dm;
    for (int t = 1; t <= 4; t++) {
        memset(bm, 0, 4096); memset(b2m, 0, 4096); memset(dm, 0, 4096);
        d[0] = 1;                              /* semaphore value, already satisfied */
        uint32_t *p = bm, *q = b2m;
        p = sdi(p, dva + 16, 1);               /* A: before */
        if (t == 1) {                          /* MI_SEMAPHORE_WAIT, poll, SAD == SDD */
            *p++ = (0x1Cu << 23) | (1u << 15) | (4u << 12) | 2;
            *p++ = 1; *p++ = (uint32_t)dva; *p++ = (uint32_t)(dva >> 32);
        } else if (t == 2 || t == 3) {         /* BBS to the other BO */
            *p++ = (0x31u << 23) | ((t == 2) ? (1u << 22) : 0) | (1u << 8) | 1;
            *p++ = (uint32_t)b2va; *p++ = (uint32_t)(b2va >> 32);
            q = sdi(q, dva + 20, 1);           /* B: in the other BO */
            *q++ = 0x05000000; *q++ = 0;       /* BBE */
        } else {                               /* MI_FLUSH_DW, post-sync timestamp */
            *p++ = (0x26u << 23) | (3u << 14) | 3;
            *p++ = (uint32_t)(dva + 32); *p++ = (uint32_t)((dva + 32) >> 32); *p++ = 0; *p++ = 0;
        }
        p = sdi(p, dva + 24, 1);               /* C: after */
        *p++ = 0x05000000; *p++ = 0;
        struct drm_i915_gem_exec_object2 obj[3] = {
            { .handle = dh, .offset = dva, .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_WRITE },
            { .handle = b2h, .offset = b2va, .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS },
            { .handle = bh, .offset = bva, .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS },
        };
        struct drm_i915_gem_execbuffer2 eb = { .buffers_ptr = (uintptr_t)obj, .buffer_count = 3,
            .batch_len = 4096, .flags = I915_EXEC_BLT | I915_EXEC_NO_RELOC };
        int r = ioctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &eb);
        int e = r ? errno : 0;
        if (!r) { struct drm_i915_gem_wait w = { .bo_handle = bh, .timeout_ns = 2000000000 }; ioctl(fd, DRM_IOCTL_I915_GEM_WAIT, &w); }
        static const char *name[] = { "", "MI_SEMAPHORE_WAIT", "BBS 2nd level to other BO", "BBS chain to other BO", "MI_FLUSH_DW timestamp" };
        printf("%-26s execbuf=%s A=%u B=%u C=%u ts=%llu\n", name[t], r ? strerror(e) : "ok",
               d[4], d[5], d[6], (unsigned long long)(d[8] | ((uint64_t)d[9] << 32)));
    }
    return 0;
}
