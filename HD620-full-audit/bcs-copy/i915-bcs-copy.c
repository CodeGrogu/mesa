/* Private, render-node-only i915 Gen9 BCS linear buffer copy fixture for the KBL GT2 HD 620.
 *
 * Correctness first: every copy uses fresh per-iteration source data, canaried destination
 * and guard pages, and full byte verification of both buffers. Timing separates single-copy
 * latency, queued submissions and in-batch throughput, and compares a CPU memcpy baseline.
 *
 * Packets come from the kernel's own Gen9 emitters (linux/drivers/gpu/drm/i915):
 *   gt/intel_gpu_commands.h  XY_SRC_COPY_BLT_CMD, GEN9_XY_FAST_COPY_BLT_CMD, BLT_*, MI_*
 *   gt/intel_migrate.c       emit_copy(): 4096-byte pitch, 1024 x 32bpp rows, 10 dwords
 *   i915_cmd_parser.c        gen9_blt_cmds / gen9_blt_regs: what the mandatory Gen9 BCS
 *                            parser accepts (2D client commands, MI_FLUSH_DW, SRM of the
 *                            BCS RING_TIMESTAMP)
 *
 * Safety: opens only a /dev/dri/renderD* node, refuses anything but i915 device 0x5916,
 * uses its own non-recoverable context, its own softpinned GEM objects and bounded waits.
 * No debugfs, sysfs writes, master/card node, reset, or global state. --selftest builds and
 * interprets the same batches on host memory without opening any device. */
#include "bcs-common.h"

#include <fcntl.h>
#include <getopt.h>
#include <i915_drm.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/utsname.h>

/* ---- Gen9 packet definitions (values from intel_gpu_commands.h) ---------------------- */
#define MI_INSTR(op, flags)          (((uint32_t)(op) << 23) | (flags))
#define MI_NOOP                      MI_INSTR(0x00, 0)
#define MI_BATCH_BUFFER_END          MI_INSTR(0x0a, 0)
#define MI_STORE_REGISTER_MEM_GEN8   MI_INSTR(0x24, 2)      /* 4 dwords */
#define MI_FLUSH_DW_GEN8             (MI_INSTR(0x26, 1) + 1) /* 4 dwords on gen8+ */
#define XY_SRC_COPY_BLT_CMD          ((2u << 29) | (0x53u << 22))
#define GEN9_XY_FAST_COPY_BLT_CMD    ((2u << 29) | (0x42u << 22))
#define BLT_WRITE_RGBA               ((2u << 20) | (1u << 20))
#define BLT_DEPTH_32                 (3u << 24)
#define BLT_ROP_SRC_COPY             (0xccu << 16)
#define BLT_RING_BASE                0x22000u               /* i915_reg.h */
#define BCS_RING_TIMESTAMP           (BLT_RING_BASE + 0x358) /* intel_engine_regs.h */

#define BLIT_DWORDS     10u
#define PITCH_BYTES     PAGE_BYTES      /* as intel_migrate.c emit_copy() */
#define ROW_PIXELS      (PAGE_BYTES / 4) /* 32bpp */
#define MAX_CHUNK_ROWS  2048u           /* 8 MiB, the kernel's CHUNK_SZ */
#define MAX_BATCH_BYTES (256u << 10)    /* parser expects < 256 KiB for known users */
#define TS_SENTINEL     0xDEADBEEFu
#define MAX_PROGRAMS    256u

enum packet { PACKET_SRC, PACKET_FAST };
static const char *packet_name[] = { "XY_SRC_COPY_BLT", "XY_FAST_COPY_BLT" };

enum mode { MODE_LATENCY, MODE_QUEUED, MODE_BATCH, MODE_CPU };
static const char *mode_name[] = { "latency", "queued", "batch", "cpu" };

struct program { uint32_t offset, length; };

/* GPU virtual addresses of the buffers a batch refers to. */
struct gpu_layout { uint64_t src, dst, ts; };

static uint32_t *emit_blit(uint32_t *cs, enum packet p, uint64_t dst, uint64_t src, uint32_t rows)
{
    if (p == PACKET_SRC) {
        *cs++ = XY_SRC_COPY_BLT_CMD | BLT_WRITE_RGBA | (BLIT_DWORDS - 2);
        *cs++ = BLT_DEPTH_32 | BLT_ROP_SRC_COPY | PITCH_BYTES;
    } else {
        *cs++ = GEN9_XY_FAST_COPY_BLT_CMD | (BLIT_DWORDS - 2); /* linear src and dst */
        *cs++ = BLT_DEPTH_32 | PITCH_BYTES;
    }
    *cs++ = 0;                          /* dst x1,y1 */
    *cs++ = rows << 16 | ROW_PIXELS;    /* dst x2,y2 (exclusive) */
    *cs++ = (uint32_t)dst;
    *cs++ = (uint32_t)(dst >> 32);
    *cs++ = 0;                          /* src x1,y1 */
    *cs++ = PITCH_BYTES;                /* src pitch */
    *cs++ = (uint32_t)src;
    *cs++ = (uint32_t)(src >> 32);
    return cs;
}

static uint32_t *emit_timestamp(uint32_t *cs, uint64_t addr)
{
    *cs++ = MI_STORE_REGISTER_MEM_GEN8;
    *cs++ = BCS_RING_TIMESTAMP;
    *cs++ = (uint32_t)addr;
    *cs++ = (uint32_t)(addr >> 32);
    return cs;
}

static unsigned chunks_for(uint64_t size)
{
    uint64_t rows = size / PITCH_BYTES;
    return (unsigned)((rows + MAX_CHUNK_ROWS - 1) / MAX_CHUNK_ROWS);
}

static uint32_t program_dwords(uint64_t size, unsigned slots)
{
    uint32_t n = 4 + slots * chunks_for(size) * BLIT_DWORDS + 4 + 4 + 1;
    return (n + 15) & ~15u; /* 64-byte aligned programs, padded with MI_NOOP */
}

/* Program p copies slots [first, first+count) and stamps ts[2p] before and ts[2p+1] after
 * a MI_FLUSH_DW, which waits for the blits to finish. */
static struct program build_program(uint32_t *base, uint32_t at_dw, unsigned p, enum packet pk,
                                    const struct gpu_layout *g, uint64_t size, unsigned first,
                                    unsigned count)
{
    uint32_t *cs = base + at_dw, *start = cs;
    cs = emit_timestamp(cs, g->ts + 8ull * p);
    for (unsigned s = first; s < first + count; s++) {
        uint64_t rows = size / PITCH_BYTES, done = 0;
        while (done < rows) {
            uint32_t r = rows - done > MAX_CHUNK_ROWS ? MAX_CHUNK_ROWS : (uint32_t)(rows - done);
            uint64_t off = slot_offset(size, s) + done * PITCH_BYTES;
            cs = emit_blit(cs, pk, g->dst + off, g->src + off, r);
            done += r;
        }
    }
    *cs++ = MI_FLUSH_DW_GEN8;
    *cs++ = 0;
    *cs++ = 0;
    *cs++ = 0;
    cs = emit_timestamp(cs, g->ts + 8ull * p + 4);
    *cs++ = MI_BATCH_BUFFER_END;
    uint32_t want = program_dwords(size, count);
    while ((uint32_t)(cs - start) < want)
        *cs++ = MI_NOOP;
    return (struct program){ at_dw * 4, want * 4 };
}

/* ---- host interpreter for --selftest: decodes exactly what the GPU would execute -------- */
struct host_va { uint64_t va; uint8_t *ptr; uint64_t bytes; };

static uint8_t *host_ptr(const struct host_va *m, int n, uint64_t va, uint64_t len)
{
    for (int i = 0; i < n; i++)
        if (va >= m[i].va && va + len <= m[i].va + m[i].bytes)
            return m[i].ptr + (va - m[i].va);
    return NULL;
}

/* Returns 0 on success; checks each header's length field as the Gen9 parser would. */
static int interpret(const uint32_t *cs, uint32_t bytes, const struct host_va *m, int n,
                     uint32_t fake_ts)
{
    const uint32_t *end = cs + bytes / 4;
    while (cs < end) {
        uint32_t h = *cs, client = h >> 29;
        if (h == MI_NOOP) { cs++; continue; }
        if (h == MI_BATCH_BUFFER_END) return 0;
        if (client == 0) {
            uint32_t len = (h & 0xff) + 2;
            if (cs + len > end) return -1;
            if (h == MI_STORE_REGISTER_MEM_GEN8) {
                if (cs[1] != BCS_RING_TIMESTAMP) return -2;
                uint32_t *dst = (uint32_t *)host_ptr(m, n, cs[2] | (uint64_t)cs[3] << 32, 4);
                if (!dst) return -3;
                *dst = fake_ts;
            } else if (h != MI_FLUSH_DW_GEN8) {
                return -4;
            }
            cs += len;
        } else if (client == 2) {
            uint32_t len = (h & 0xff) + 2, op = (h >> 22) & 0x7f;
            if (len != BLIT_DWORDS || cs + len > end) return -5;
            if (op != 0x53 && op != 0x42) return -6;
            if ((cs[1] & 0xffff) != PITCH_BYTES || cs[7] != PITCH_BYTES) return -7;
            if ((cs[1] & (7u << 24)) != BLT_DEPTH_32) return -8;
            if (op == 0x53 && ((cs[1] >> 16) & 0xff) != 0xcc) return -9;
            if (cs[2] != 0 || cs[6] != 0) return -10;
            uint32_t w = cs[3] & 0xffff, rows = cs[3] >> 16;
            uint64_t dva = cs[4] | (uint64_t)cs[5] << 32, sva = cs[8] | (uint64_t)cs[9] << 32;
            uint64_t span = (uint64_t)(rows - 1) * PITCH_BYTES + w * 4ull;
            uint8_t *d = host_ptr(m, n, dva, span), *s = host_ptr(m, n, sva, span);
            if (!d || !s || !rows || !w) return -11;
            for (uint32_t r = 0; r < rows; r++)
                memcpy(d + (uint64_t)r * PITCH_BYTES, s + (uint64_t)r * PITCH_BYTES, w * 4u);
            cs += len;
        } else {
            return -12;
        }
    }
    return -13; /* no MI_BATCH_BUFFER_END inside batch_len: the parser rejects this */
}

/* ---- device access ------------------------------------------------------------------- */
static int dev_fd = -1;
static uint32_t ctx_id;
static uint64_t next_va = 1ull << 32; /* bump allocator: softpinned addresses are never reused */

static int xioctl(unsigned long req, void *arg)
{
    int r;
    do r = ioctl(dev_fd, req, arg);
    while (r < 0 && (errno == EINTR || errno == EAGAIN));
    return r < 0 ? -errno : 0;
}

static void die_ioctl(const char *what, int err, int code)
{
    fprintf(stderr, "%s: %s\n", what, strerror(-err));
    exit(code);
}
#define DRM(req, arg) do { int e_ = xioctl(req, arg); if (e_) die_ioctl(#req, e_, EXIT_GPU); } while (0)

static int getparam(int param, int *value)
{
    struct drm_i915_getparam gp = { .param = param, .value = value };
    return xioctl(DRM_IOCTL_I915_GETPARAM, &gp);
}

struct bo { uint32_t handle; uint64_t bytes, va; uint8_t *map; };

static struct bo bo_new(uint64_t bytes)
{
    struct bo b = { .bytes = (bytes + PAGE_BYTES - 1) & ~(uint64_t)(PAGE_BYTES - 1) };
    struct drm_i915_gem_create c = { .size = b.bytes };
    DRM(DRM_IOCTL_I915_GEM_CREATE, &c);
    b.handle = c.handle;
    b.bytes = c.size;
    b.va = next_va;
    next_va += (b.bytes + (2u << 20) - 1) & ~(uint64_t)((2u << 20) - 1);
    next_va += 2u << 20; /* unmapped gap between objects */
    struct drm_i915_gem_mmap_offset mo = { .handle = b.handle, .flags = I915_MMAP_OFFSET_WB };
    DRM(DRM_IOCTL_I915_GEM_MMAP_OFFSET, &mo);
    b.map = mmap(NULL, b.bytes, PROT_READ | PROT_WRITE, MAP_SHARED, dev_fd, mo.offset);
    if (b.map == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_GPU);
    }
    return b;
}

static void bo_free(struct bo *b)
{
    munmap(b->map, b->bytes);
    struct drm_gem_close c = { .handle = b->handle };
    DRM(DRM_IOCTL_GEM_CLOSE, &c);
    b->handle = 0;
}

/* Only called on idle objects (after a successful bounded wait), so it never blocks long. */
static void bo_cpu(struct bo *b, int write)
{
    struct drm_i915_gem_set_domain d = { .handle = b->handle, .read_domains = I915_GEM_DOMAIN_CPU,
        .write_domain = write ? I915_GEM_DOMAIN_CPU : 0 };
    DRM(DRM_IOCTL_I915_GEM_SET_DOMAIN, &d);
}

static int64_t timeout_ns = 2000000000ll;

static void bo_wait(struct bo *b)
{
    struct drm_i915_gem_wait w = { .bo_handle = b->handle, .timeout_ns = timeout_ns };
    int e = xioctl(DRM_IOCTL_I915_GEM_WAIT, &w);
    if (e == -ETIME) {
        fprintf(stderr, "bounded wait of %" PRId64 " ns expired; exiting (context is "
                "non-recoverable, closing the fd releases it)\n", timeout_ns);
        exit(EXIT_GPU);
    }
    if (e)
        die_ioctl("DRM_IOCTL_I915_GEM_WAIT", e, EXIT_GPU);
}

static int open_device(const char *node, uint32_t *devid, int *ts_freq, int *parser)
{
    char real[PATH_MAX];
    struct stat st;
    if (!realpath(node, real) || strncmp(real, "/dev/dri/renderD", 16)) {
        fprintf(stderr, "%s is not a render node; refusing\n", node);
        return EXIT_USAGE;
    }
    dev_fd = open(real, O_RDWR | O_CLOEXEC);
    if (dev_fd < 0) {
        perror(real);
        return EXIT_DEVICE;
    }
    if (fstat(dev_fd, &st) || !S_ISCHR(st.st_mode) || minor(st.st_rdev) < 128) {
        fprintf(stderr, "%s is not a DRM render-node character device; refusing\n", real);
        return EXIT_USAGE;
    }
    char name[16] = {0};
    struct drm_version v = { .name = name, .name_len = sizeof(name) - 1 };
    if (xioctl(DRM_IOCTL_VERSION, &v) || strcmp(name, "i915")) {
        fprintf(stderr, "%s: driver '%s' is not i915; refusing\n", node, name);
        return EXIT_DEVICE;
    }
    int id = 0, blt = 0, softpin = 0;
    if (getparam(I915_PARAM_CHIPSET_ID, &id) || (uint32_t)id != KBL_GT2_HD620_DEVID) {
        fprintf(stderr, "device id 0x%04x is not the KBL GT2 HD 620 (0x%04x); refusing\n", id,
                KBL_GT2_HD620_DEVID);
        return EXIT_DEVICE;
    }
    if (getparam(I915_PARAM_HAS_BLT, &blt) || !blt ||
        getparam(I915_PARAM_HAS_EXEC_SOFTPIN, &softpin) || !softpin ||
        getparam(I915_PARAM_CS_TIMESTAMP_FREQUENCY, ts_freq) || *ts_freq <= 0) {
        fprintf(stderr, "missing BLT, softpin or CS timestamp frequency; refusing\n");
        return EXIT_DEVICE;
    }
    if (getparam(I915_PARAM_CMD_PARSER_VERSION, parser))
        *parser = -1;
    *devid = (uint32_t)id;

    struct drm_i915_gem_context_create_ext cc = {0};
    DRM(DRM_IOCTL_I915_GEM_CONTEXT_CREATE_EXT, &cc);
    ctx_id = cc.ctx_id;
    /* A hang bans this context instead of replaying it. */
    struct drm_i915_gem_context_param p = { .ctx_id = ctx_id,
        .param = I915_CONTEXT_PARAM_RECOVERABLE, .value = 0 };
    DRM(DRM_IOCTL_I915_GEM_CONTEXT_SETPARAM, &p);
    return EXIT_OK;
}

static void close_device(void)
{
    struct drm_i915_gem_context_destroy d = { .ctx_id = ctx_id };
    if (ctx_id)
        xioctl(DRM_IOCTL_I915_GEM_CONTEXT_DESTROY, &d);
    close(dev_fd);
}

/* ---- measurement ------------------------------------------------------------------- */
struct config {
    unsigned iters, warmup, queue, batch, verify_only;
    uint64_t max_working_set;
    int ts_freq;
    enum packet packet;
};

static int failures;

static void report_mismatch(const char *route, const char *mode, uint64_t size, unsigned it,
                            const struct verify_result *r)
{
    printf("{\"record\":\"mismatch\",\"route\":\"%s\",\"mode\":\"%s\",\"size_bytes\":%" PRIu64
           ",\"iteration\":%u,\"bad_words\":%" PRIu64 ",\"first\":\"%s\"}\n",
           route, mode, size, it, r->bad_words, r->first);
    fflush(stdout);
    failures++;
}

static unsigned slots_for(enum mode m, const struct config *c, uint64_t size)
{
    uint64_t cap = c->max_working_set / size;
    unsigned want = m == MODE_QUEUED ? c->queue : m == MODE_BATCH ? c->batch : 1;
    if (cap < 1)
        cap = 1;
    return want < cap ? want : (unsigned)cap;
}

static void print_common(const char *route, enum mode m, uint64_t size, unsigned slots,
                         unsigned subs, const struct config *c)
{
    printf("{\"record\":\"result\",\"route\":\"%s\",\"packet\":\"%s\",\"mode\":\"%s\","
           "\"size_bytes\":%" PRIu64 ",\"copies_per_sample\":%u,\"submissions_per_sample\":%u,"
           "\"bytes_per_sample\":%" PRIu64 ",\"working_set_bytes\":%" PRIu64
           ",\"warmup\":%u,\"samples\":%u",
           route, m == MODE_CPU ? "memcpy" : packet_name[c->packet], mode_name[m], size, slots,
           subs, size * slots, 2 * layout_bytes(size, slots), c->warmup, c->iters);
}

static void run_cpu(uint64_t size, const struct config *c)
{
    struct bo src = bo_new(layout_bytes(size, 1)), dst = bo_new(layout_bytes(size, 1));
    double *t = calloc(c->iters, sizeof(*t));
    struct verify_result vr = {0};
    int f0 = gt_act_freq_mhz(dev_fd);
    for (unsigned it = 0; it < c->warmup + c->iters; it++) {
        bo_cpu(&src, 1);
        bo_cpu(&dst, 1);
        prepare_buffers(src.map, dst.map, size, 1, 0x435055, it);
        uint64_t t0 = now_ns();
        memcpy(dst.map + slot_offset(size, 0), src.map + slot_offset(size, 0), size);
        uint64_t t1 = now_ns();
        struct verify_result r = {0};
        verify_buffers(src.map, dst.map, size, 1, 0x435055, it, &r);
        if (r.bad_words)
            report_mismatch("cpu", "cpu", size, it, &r);
        vr.bytes_checked += r.bytes_checked;
        vr.bad_words += r.bad_words;
        if (it >= c->warmup)
            t[it - c->warmup] = (double)(t1 - t0);
    }
    struct stats s = summarize(t, c->iters);
    print_common("cpu", MODE_CPU, size, 1, 0, c);
    json_stats(stdout, "host_ns", t, c->iters);
    printf(",\"host_GBps_p50\":%.3f,\"freq_mhz_before\":%d,\"freq_mhz_after\":%d,"
           "\"verified_bytes\":%" PRIu64 ",\"bad_words\":%" PRIu64 "}\n",
           gbps((double)size, s.p50), f0, gt_act_freq_mhz(dev_fd), vr.bytes_checked, vr.bad_words);
    fflush(stdout);
    free(t);
    bo_free(&src);
    bo_free(&dst);
}

static void run_bcs(enum mode m, uint64_t size, const struct config *c)
{
    unsigned slots = slots_for(m, c, size);
    unsigned progs = m == MODE_QUEUED ? slots : 1;
    uint32_t per = program_dwords(size, m == MODE_QUEUED ? 1 : slots);
    if ((uint64_t)per * 4 * progs > MAX_BATCH_BYTES || progs > MAX_PROGRAMS) {
        fprintf(stderr, "batch for %s %" PRIu64 " x %u exceeds %u bytes; lower --queue/--batch\n",
                mode_name[m], size, slots, MAX_BATCH_BYTES);
        exit(EXIT_USAGE);
    }
    struct bo src = bo_new(layout_bytes(size, slots)), dst = bo_new(layout_bytes(size, slots));
    struct bo ts = bo_new(PAGE_BYTES), batch = bo_new((uint64_t)per * 4 * progs);
    struct gpu_layout g = { src.va, dst.va, ts.va };
    struct program prog[MAX_PROGRAMS];
    bo_cpu(&batch, 1);
    for (unsigned p = 0; p < progs; p++)
        prog[p] = m == MODE_QUEUED ? build_program((uint32_t *)batch.map, p * per, p, c->packet,
                                                   &g, size, p, 1)
                                   : build_program((uint32_t *)batch.map, 0, 0, c->packet, &g,
                                                   size, 0, slots);

    struct drm_i915_gem_exec_object2 obj[4] = {
        { .handle = src.handle, .offset = src.va,
          .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS },
        { .handle = dst.handle, .offset = dst.va,
          .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_WRITE },
        { .handle = ts.handle, .offset = ts.va,
          .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_WRITE },
        { .handle = batch.handle, .offset = batch.va,
          .flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS },
    };
    unsigned n = c->iters;
    double *host = calloc(n, sizeof(double)), *submit = calloc(n, sizeof(double));
    double *gpu = calloc(n, sizeof(double)), *gpu_sum = calloc(n, sizeof(double));
    struct verify_result vr = {0};
    uint64_t route = 0x424353 ^ ((uint64_t)c->packet << 32);
    uint64_t copy0 = 0, render0 = 0, wall0 = 0;
    int ok, have_fdinfo = 1, f0 = -1;
    double host_sum = 0;

    for (unsigned it = 0; it < c->warmup + n; it++) {
        if (it == c->warmup) {
            copy0 = fdinfo_value(dev_fd, "drm-engine-copy", &ok);
            have_fdinfo &= ok;
            render0 = fdinfo_value(dev_fd, "drm-engine-render", &ok);
            have_fdinfo &= ok;
            f0 = gt_act_freq_mhz(dev_fd);
            wall0 = now_ns();
        }
        bo_cpu(&src, 1);
        bo_cpu(&dst, 1);
        bo_cpu(&ts, 1);
        prepare_buffers(src.map, dst.map, size, slots, route, it);
        uint32_t *stamp = (uint32_t *)ts.map;
        for (unsigned i = 0; i < 2 * progs; i++)
            stamp[i] = TS_SENTINEL;

        uint64_t sub = 0, t0 = now_ns();
        for (unsigned p = 0; p < progs; p++) {
            struct drm_i915_gem_execbuffer2 eb = { .buffers_ptr = (uintptr_t)obj,
                .buffer_count = 4, .batch_start_offset = prog[p].offset,
                .batch_len = prog[p].length, .flags = I915_EXEC_BLT | I915_EXEC_NO_RELOC };
            i915_execbuffer2_set_context_id(eb, ctx_id);
            uint64_t a = now_ns();
            DRM(DRM_IOCTL_I915_GEM_EXECBUFFER2, &eb);
            sub += now_ns() - a;
        }
        bo_wait(&ts);
        bo_wait(&dst);
        uint64_t t1 = now_ns();

        bo_cpu(&ts, 0);
        bo_cpu(&src, 0);
        bo_cpu(&dst, 0);
        double gsum = 0;
        for (unsigned p = 0; p < progs; p++) {
            if (stamp[2 * p] == TS_SENTINEL || stamp[2 * p + 1] == TS_SENTINEL) {
                fprintf(stderr, "timestamp %u not written; batch did not run as built\n", p);
                exit(EXIT_GPU);
            }
            gsum += (double)(uint32_t)(stamp[2 * p + 1] - stamp[2 * p]);
        }
        double span = (double)(uint32_t)(stamp[2 * progs - 1] - stamp[0]);
        struct verify_result r = {0};
        verify_buffers(src.map, dst.map, size, slots, route, it, &r);
        if (r.bad_words)
            report_mismatch("bcs", mode_name[m], size, it, &r);
        vr.bytes_checked += r.bytes_checked;
        vr.bad_words += r.bad_words;
        if (it >= c->warmup) {
            unsigned k = it - c->warmup;
            host[k] = (double)(t1 - t0);
            submit[k] = (double)sub;
            gpu[k] = span * 1e9 / c->ts_freq;
            gpu_sum[k] = gsum * 1e9 / c->ts_freq;
            host_sum += host[k];
        }
    }
    uint64_t wall = now_ns() - wall0;
    uint64_t copy_ns = fdinfo_value(dev_fd, "drm-engine-copy", &ok) - copy0;
    uint64_t render_ns = fdinfo_value(dev_fd, "drm-engine-render", &ok) - render0;

    if (!c->verify_only) {
        struct stats hs = summarize(host, n), gs = summarize(gpu, n);
        double bytes = (double)size * slots;
        print_common("bcs", m, size, slots, progs, c);
        json_stats(stdout, "host_ns", host, n);
        json_stats(stdout, "submit_ioctl_ns", submit, n);
        json_stats(stdout, "gpu_span_ns", gpu, n);
        json_stats(stdout, "gpu_program_sum_ns", gpu_sum, n);
        printf(",\"host_GBps_p50\":%.3f,\"gpu_GBps_p50\":%.3f,\"host_GBps_aggregate\":%.3f",
               gbps(bytes, hs.p50), gbps(bytes, gs.p50), gbps(bytes * n, host_sum));
        if (have_fdinfo)
            printf(",\"fdinfo_copy_ns\":%" PRIu64 ",\"fdinfo_render_ns\":%" PRIu64
                   ",\"fdinfo_copy_ns_per_sample\":%.0f,\"copy_busy_vs_host\":%.3f",
                   copy_ns, render_ns, (double)copy_ns / n, host_sum > 0 ? copy_ns / host_sum : 0);
        else
            printf(",\"fdinfo\":null");
        printf(",\"phase_wall_ns\":%" PRIu64 ",\"freq_mhz_before\":%d,\"freq_mhz_after\":%d,"
               "\"verified_bytes\":%" PRIu64 ",\"bad_words\":%" PRIu64 "}\n",
               wall, f0, gt_act_freq_mhz(dev_fd), vr.bytes_checked, vr.bad_words);
    } else {
        printf("{\"record\":\"verify\",\"route\":\"bcs\",\"packet\":\"%s\",\"mode\":\"%s\","
               "\"size_bytes\":%" PRIu64 ",\"copies\":%u,\"iterations\":%u,\"verified_bytes\":%"
               PRIu64 ",\"bad_words\":%" PRIu64 ",\"fdinfo_copy_ns\":%" PRIu64
               ",\"fdinfo_render_ns\":%" PRIu64 "}\n",
               packet_name[c->packet], mode_name[m], size, slots, c->warmup + n, vr.bytes_checked,
               vr.bad_words, copy_ns, render_ns);
    }
    if (have_fdinfo && (copy_ns == 0 || render_ns != 0)) {
        fprintf(stderr, "engine accounting does not show a copy-engine-only run (copy %" PRIu64
                " ns, render %" PRIu64 " ns)\n", copy_ns, render_ns);
        failures++;
    }
    fflush(stdout);
    free(host);
    free(submit);
    free(gpu);
    free(gpu_sum);
    bo_free(&batch);
    bo_free(&ts);
    bo_free(&dst);
    bo_free(&src);
}

/* ---- self-test: build every batch shape and execute it on host memory ------------------ */
static int selftest(void)
{
    static const uint64_t sizes[] = { 4096, 8192, 12288, 1 << 20, 8 << 20, (8 << 20) + 4096,
                                      (24 << 20) + 12288 };
    int bad = 0;
    for (int pk = 0; pk < 2; pk++)
        for (unsigned i = 0; i < sizeof(sizes) / sizeof(*sizes); i++)
            for (unsigned slots = 1; slots <= 3; slots += 2) {
                uint64_t size = sizes[i], lb = layout_bytes(size, slots);
                uint8_t *src = malloc(lb), *dst = malloc(lb);
                uint32_t ts[8];
                uint32_t per = program_dwords(size, slots);
                uint32_t *batch = calloc(per, 4);
                struct gpu_layout g = { 1ull << 32, 3ull << 32, 5ull << 32 };
                struct host_va m[3] = { { g.src, src, lb }, { g.dst, dst, lb },
                                        { g.ts, (uint8_t *)ts, sizeof(ts) } };
                prepare_buffers(src, dst, size, slots, 7 + pk, i);
                struct program p = build_program(batch, 0, 0, pk, &g, size, 0, slots);
                int rc = interpret(batch, p.length, m, 3, 1234);
                struct verify_result r = {0};
                verify_buffers(src, dst, size, slots, 7 + pk, i, &r);
                /* The verifier must also catch a single flipped byte in a guard and a payload. */
                dst[lb - 1] ^= 1;
                dst[slot_offset(size, slots - 1) + size / 2] ^= 0x80;
                struct verify_result neg = {0};
                verify_buffers(src, dst, size, slots, 7 + pk, i, &neg);
                int fail = rc || r.bad_words || neg.bad_words != 2 || ts[0] != 1234 ||
                           ts[1] != 1234;
                printf("selftest %-16s size %10" PRIu64 " slots %u chunks %u dwords %4u: %s",
                       packet_name[pk], size, slots, chunks_for(size), per,
                       fail ? "FAIL" : "ok");
                if (fail)
                    printf(" (interpret %d, bad %" PRIu64 ", negative %" PRIu64 ")", rc,
                           r.bad_words, neg.bad_words);
                printf("\n");
                bad |= fail;
                free(src);
                free(dst);
                free(batch);
            }
    uint64_t parsed[MAX_SIZES];
    if (parse_sizes("4K,8M+4K,1M", parsed, MAX_SIZES) != 3 || parsed[1] != (8 << 20) + 4096 ||
        parse_sizes("100", parsed, MAX_SIZES) != -1 || parse_sizes("512M", parsed, MAX_SIZES) != -1)
        bad = 1, printf("selftest size parsing: FAIL\n");
    printf("selftest %s\n", bad ? "FAILED" : "passed");
    return bad ? EXIT_MISMATCH : EXIT_OK;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s [--selftest] [--verify] [--modes cpu,latency,queued,batch]\n"
        "          [--sizes 4K,64K,1M,8M,64M] [--iters N] [--warmup N] [--queue K] [--batch N]\n"
        "          [--max-working-set BYTES] [--packet src|fast] [--timeout-ms MS] [--node PATH]\n"
        "Output is JSON lines on stdout. Times in ns, bandwidth in GB/s (1e9 bytes/s).\n", argv0);
}

int main(int argc, char **argv)
{
    struct config c = { .iters = 50, .warmup = 5, .queue = 16, .batch = 16,
                        .max_working_set = 64ull << 20, .packet = PACKET_SRC };
    const char *node = "/dev/dri/renderD128", *modes = "cpu,latency,queued,batch";
    const char *size_arg = "4K,64K,1M,8M,64M";
    int do_selftest = 0;
    static const struct option opts[] = {
        { "selftest", no_argument, 0, 'S' }, { "verify", no_argument, 0, 'V' },
        { "modes", required_argument, 0, 'm' }, { "sizes", required_argument, 0, 's' },
        { "iters", required_argument, 0, 'i' }, { "warmup", required_argument, 0, 'w' },
        { "queue", required_argument, 0, 'q' }, { "batch", required_argument, 0, 'b' },
        { "max-working-set", required_argument, 0, 'W' },
        { "packet", required_argument, 0, 'p' }, { "timeout-ms", required_argument, 0, 't' },
        { "node", required_argument, 0, 'n' }, { 0, 0, 0, 0 } };
    for (int o; (o = getopt_long(argc, argv, "", opts, NULL)) != -1;) {
        switch (o) {
        case 'S': do_selftest = 1; break;
        case 'V': c.verify_only = 1; break;
        case 'm': modes = optarg; break;
        case 's': size_arg = optarg; break;
        case 'i': c.iters = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'w': c.warmup = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'q': c.queue = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'b': c.batch = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'W': c.max_working_set = strtoull(optarg, NULL, 0); break;
        case 'p':
            if (!strcmp(optarg, "src")) c.packet = PACKET_SRC;
            else if (!strcmp(optarg, "fast")) c.packet = PACKET_FAST;
            else { usage(argv[0]); return EXIT_USAGE; }
            break;
        case 't': timeout_ns = (int64_t)strtoul(optarg, NULL, 10) * 1000000ll; break;
        case 'n': node = optarg; break;
        default: usage(argv[0]); return EXIT_USAGE;
        }
    }
    if (do_selftest)
        return selftest(); /* never opens a device */

    uint64_t sizes[MAX_SIZES];
    int nsizes = parse_sizes(size_arg, sizes, MAX_SIZES);
    if (nsizes <= 0 || c.iters < 1 || c.iters > 10000 || c.warmup > 1000 || c.queue < 1 ||
        c.queue > MAX_PROGRAMS || c.batch < 1 || c.batch > MAX_PROGRAMS ||
        c.max_working_set < PAGE_BYTES || c.max_working_set > (512ull << 20) ||
        timeout_ns < 1000000 || timeout_ns > 10000000000ll) {
        usage(argv[0]);
        fprintf(stderr, "limits: sizes 4K-aligned <= 256M, iters 1-10000, warmup <= 1000, "
                        "queue/batch 1-%u, working set <= 512M, timeout 1-10000 ms\n", MAX_PROGRAMS);
        return EXIT_USAGE;
    }
    int want[4] = {0};
    for (int m = 0; m < 4; m++)
        want[m] = strstr(modes, mode_name[m]) != NULL;
    if (c.verify_only) {
        /* Correctness matrix: chunk boundaries, remainders, slot guards, both packets. */
        static const char *vsizes = "4K,8K,12K,1M,8M,8M+4K,24M+12K";
        nsizes = parse_sizes(vsizes, sizes, MAX_SIZES);
        c.iters = 3;
        c.warmup = 0;
        c.queue = 4;
        c.batch = 4;
        c.max_working_set = 128ull << 20;
        want[MODE_CPU] = 0;
        want[MODE_LATENCY] = want[MODE_QUEUED] = want[MODE_BATCH] = 1;
    }

    uint32_t devid;
    int parser, rc = open_device(node, &devid, &c.ts_freq, &parser);
    if (rc)
        return rc;
    struct utsname u;
    uname(&u);
    printf("{\"record\":\"header\",\"fixture\":\"i915-bcs-copy\",\"node\":\"%s\",\"device_id\":"
           "\"0x%04x\",\"kernel\":\"%s\",\"cmd_parser_version\":%d,\"cs_timestamp_hz\":%d,"
           "\"packet\":\"%s\",\"pitch_bytes\":%u,\"max_chunk_rows\":%u,\"guard_bytes\":%u,"
           "\"timeout_ns\":%" PRId64 ",\"sequence\":\"for each size in order: %s%s%s%s\","
           "\"units\":\"ns; GB/s = 1e9 bytes/s; sizes in bytes\"}\n",
           node, devid, u.release, parser, c.ts_freq, packet_name[c.packet], PITCH_BYTES,
           MAX_CHUNK_ROWS, GUARD_BYTES, timeout_ns, want[MODE_CPU] ? "cpu " : "",
           want[MODE_LATENCY] ? "latency " : "", want[MODE_QUEUED] ? "queued " : "",
           want[MODE_BATCH] ? "batch" : "");
    fflush(stdout);

    for (int pk = 0; pk < (c.verify_only ? 2 : 1); pk++) {
        if (c.verify_only)
            c.packet = pk;
        for (int i = 0; i < nsizes; i++) {
            if (want[MODE_CPU]) run_cpu(sizes[i], &c);
            if (want[MODE_LATENCY]) run_bcs(MODE_LATENCY, sizes[i], &c);
            if (want[MODE_QUEUED]) run_bcs(MODE_QUEUED, sizes[i], &c);
            if (want[MODE_BATCH]) run_bcs(MODE_BATCH, sizes[i], &c);
        }
    }
    close_device();
    printf("{\"record\":\"summary\",\"failures\":%d}\n", failures);
    return failures ? EXIT_MISMATCH : EXIT_OK;
}
