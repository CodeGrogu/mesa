/* Does MI_CONDITIONAL_BATCH_BUFFER_END in a second-level batch on the Gen9
 * video engine end only that level, or the whole batch?
 *
 * First level:  SDI A=1; MI_BATCH_BUFFER_START(2nd level); SDI C=1; BBE
 * Second level: MI_CONDITIONAL_BATCH_BUFFER_END(mem=M, data=0); SDI B=1; BBE
 * Gen9 continues only if mem > data.  Run with M=0 (terminate) and M=1.
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
#define DRM(req, arg) do { if (ioctl(fd, req, arg)) { perror(#req); exit(2); } } while (0)

static uint32_t create(uint64_t size, void **map)
{
    struct drm_i915_gem_create c = { .size = size };
    DRM(DRM_IOCTL_I915_GEM_CREATE, &c);
    struct drm_i915_gem_mmap_offset mo = { .handle = c.handle, .flags = I915_MMAP_OFFSET_WB };
    DRM(DRM_IOCTL_I915_GEM_MMAP_OFFSET, &mo);
    *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mo.offset);
    if (*map == MAP_FAILED) { perror("mmap"); exit(2); }
    memset(*map, 0, size);
    return c.handle;
}

static uint32_t *sdi(uint32_t *p, uint64_t addr, uint32_t v)
{
    *p++ = (0x20u << 23) | 2;            /* MI_STORE_DATA_IMM, 4 dwords */
    *p++ = (uint32_t)addr; *p++ = (uint32_t)(addr >> 32); *p++ = v;
    return p;
}

int main(void)
{
    fd = open("/dev/dri/renderD128", O_RDWR);
    if (fd < 0) { perror("open"); return 2; }
    const uint64_t batch_va = 1ull << 32, data_va = (1ull << 32) + (1 << 20);
    void *bmap, *dmap;
    uint32_t bh = create(8192, &bmap), dh = create(4096, &dmap);
    volatile uint32_t *d = dmap;

    for (int m = 0; m <= 1; m++) {
        memset(dmap, 0, 4096);
        d[0] = m;                                       /* compared value M */
        uint32_t *p = bmap;
        p = sdi(p, data_va + 16, 1);                    /* A */
        *p++ = (0x31u << 23) | (1u << 22) | (1u << 8) | 1; /* BBS 2nd level, PPGTT */
        *p++ = (uint32_t)(batch_va + 4096); *p++ = (uint32_t)((batch_va + 4096) >> 32);
        p = sdi(p, data_va + 24, 1);                    /* C */
        *p++ = 0x05000000;                              /* MI_BATCH_BUFFER_END */
        *p++ = 0;
        uint32_t *q = (uint32_t *)((char *)bmap + 4096);
        *q++ = (0x36u << 23) | (1u << 21) | 2;          /* MI_COND_BBE, compare semaphore */
        *q++ = 0;                                       /* data dword */
        *q++ = (uint32_t)data_va; *q++ = (uint32_t)(data_va >> 32);
        q = sdi(q, data_va + 20, 1);                    /* B */
        *q++ = 0x05000000;
        *q++ = 0;

        struct drm_i915_gem_exec_object2 obj[2] = {
            { .handle = dh, .offset = data_va,
              .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_WRITE },
            { .handle = bh, .offset = batch_va,
              .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS },
        };
        struct drm_i915_gem_execbuffer2 eb = { .buffers_ptr = (uintptr_t)obj, .buffer_count = 2,
            .batch_len = 4096, .flags = I915_EXEC_BSD | I915_EXEC_NO_RELOC };
        DRM(DRM_IOCTL_I915_GEM_EXECBUFFER2, &eb);
        struct drm_i915_gem_wait w = { .bo_handle = bh, .timeout_ns = 2000000000 };
        DRM(DRM_IOCTL_I915_GEM_WAIT, &w);
        printf("M=%d: A=%u B(2nd level after COND_BBE)=%u C(1st level after return)=%u\n",
               m, d[4], d[5], d[6]);
    }
    return 0;
}
