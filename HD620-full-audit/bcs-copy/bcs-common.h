/* Shared helpers for the HD 620 copy fixtures: deterministic patterns, canaries,
 * statistics, per-fd i915 engine accounting (fdinfo) and read-only GT frequency.
 * Nothing here opens a device or touches GPU state. */
#ifndef BCS_COMMON_H
#define BCS_COMMON_H

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#define KBL_GT2_HD620_DEVID 0x5916u
#define PAGE_BYTES 4096u
#define GUARD_BYTES 4096u
#define MAX_COPY_BYTES (256ull << 20)
#define MAX_SIZES 32

/* Exit codes shared by both fixtures. */
enum { EXIT_OK = 0, EXIT_USAGE = 1, EXIT_DEVICE = 2, EXIT_MISMATCH = 3, EXIT_GPU = 4 };

static inline uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return 1000000000ull * (uint64_t)t.tv_sec + (uint64_t)t.tv_nsec;
}

/* splitmix64 finalizer: a fixed, documented bijection, so every word is reproducible. */
static inline uint64_t mix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

#define CANARY_SEED 0xC0FFEE5A5A5A5A5Aull

/* Seed differs per route, size, iteration and slot, so a stale result from an
 * earlier iteration or a copy into the wrong slot can never verify. */
static inline uint64_t payload_seed(uint64_t route, uint64_t size, uint64_t iter, uint64_t slot)
{
    return mix64(mix64(mix64(route ^ (size << 1)) ^ iter) ^ (slot * 0x100000001B3ull));
}

static inline void fill_words(void *dst, size_t bytes, uint64_t seed, uint64_t first_word)
{
    uint64_t *w = dst;
    for (size_t i = 0; i < bytes / 8; i++)
        w[i] = mix64(seed + first_word + i);
}

/* Returns the number of mismatching 64-bit words and the first mismatch. */
static inline size_t check_words(const void *src, size_t bytes, uint64_t seed, uint64_t first_word,
                                 size_t *first_bad, uint64_t *got, uint64_t *want)
{
    const uint64_t *w = src;
    size_t bad = 0;
    for (size_t i = 0; i < bytes / 8; i++) {
        uint64_t e = mix64(seed + first_word + i);
        if (w[i] != e) {
            if (!bad) {
                *first_bad = i * 8;
                *got = w[i];
                *want = e;
            }
            bad++;
        }
    }
    return bad;
}

/* Layout of every src/dst buffer: [guard][slot0][guard][slot1][guard]...
 * Canary words are indexed by absolute buffer position, so a shifted write is detected. */
static inline uint64_t slot_offset(uint64_t size, unsigned slot)
{
    return GUARD_BYTES + (uint64_t)slot * (size + GUARD_BYTES);
}

static inline uint64_t layout_bytes(uint64_t size, unsigned slots)
{
    return slot_offset(size, slots);
}

struct verify_result {
    uint64_t bytes_checked;
    uint64_t bad_words;
    char first[256];
};

static inline void note_bad(struct verify_result *r, const char *what, unsigned slot, size_t off,
                            uint64_t got, uint64_t want)
{
    if (!r->first[0])
        snprintf(r->first, sizeof(r->first),
                 "%s slot %u byte offset %zu got 0x%016" PRIx64 " want 0x%016" PRIx64,
                 what, slot, off, got, want);
}

/* Fill source payloads with per-iteration data and every guard of both buffers,
 * plus the whole destination, with position-indexed canaries. */
static inline void prepare_buffers(uint8_t *src, uint8_t *dst, uint64_t size, unsigned slots,
                                   uint64_t route, uint64_t iter)
{
    uint64_t total = layout_bytes(size, slots);
    fill_words(dst, total, CANARY_SEED, 0);
    fill_words(src, total, CANARY_SEED ^ 1, 0);
    for (unsigned s = 0; s < slots; s++)
        fill_words(src + slot_offset(size, s), size, payload_seed(route, size, iter, s), 0);
}

/* Full verification: every destination byte (payload and guards) and every source byte. */
static inline void verify_buffers(const uint8_t *src, const uint8_t *dst, uint64_t size,
                                  unsigned slots, uint64_t route, uint64_t iter,
                                  struct verify_result *r)
{
    size_t off;
    uint64_t got, want;
    uint64_t total = layout_bytes(size, slots);
    for (unsigned s = 0; s <= slots; s++) {
        uint64_t g = s == 0 ? 0 : slot_offset(size, s - 1) + size;
        size_t n;
        if ((n = check_words(dst + g, GUARD_BYTES, CANARY_SEED, g / 8, &off, &got, &want))) {
            r->bad_words += n;
            note_bad(r, "dst-guard", s, g + off, got, want);
        }
        if ((n = check_words(src + g, GUARD_BYTES, CANARY_SEED ^ 1, g / 8, &off, &got, &want))) {
            r->bad_words += n;
            note_bad(r, "src-guard", s, g + off, got, want);
        }
    }
    for (unsigned s = 0; s < slots; s++) {
        uint64_t o = slot_offset(size, s), seed = payload_seed(route, size, iter, s);
        size_t n;
        if ((n = check_words(dst + o, size, seed, 0, &off, &got, &want))) {
            r->bad_words += n;
            note_bad(r, "dst-payload", s, o + off, got, want);
        }
        if ((n = check_words(src + o, size, seed, 0, &off, &got, &want))) {
            r->bad_words += n;
            note_bad(r, "src-payload", s, o + off, got, want);
        }
    }
    r->bytes_checked += 2 * total;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

struct stats { double min, p50, p95, max, mean; unsigned n; };

/* Nearest-rank percentiles over a copy of the samples. */
static inline struct stats summarize(const double *v, unsigned n)
{
    struct stats s = {0};
    if (!n)
        return s;
    double *t = malloc(n * sizeof(*t));
    if (!t)
        abort();
    memcpy(t, v, n * sizeof(*t));
    qsort(t, n, sizeof(*t), cmp_double);
    double sum = 0;
    for (unsigned i = 0; i < n; i++)
        sum += t[i];
    s.n = n;
    s.min = t[0];
    s.max = t[n - 1];
    s.p50 = t[(unsigned)ceil(0.50 * n) - 1];
    s.p95 = t[(unsigned)ceil(0.95 * n) - 1];
    s.mean = sum / n;
    free(t);
    return s;
}

static inline void json_stats(FILE *f, const char *name, const double *v, unsigned n)
{
    struct stats s = summarize(v, n);
    fprintf(f, ",\"%s\":{\"n\":%u,\"min\":%.0f,\"p50\":%.0f,\"p95\":%.0f,\"max\":%.0f,\"mean\":%.1f}",
            name, s.n, s.min, s.p50, s.p95, s.max, s.mean);
}

/* GB/s in decimal units (1e9 bytes per second) from bytes and nanoseconds. */
static inline double gbps(double bytes, double ns)
{
    return ns > 0 ? bytes / ns : 0;
}

/* i915 per-client engine time from /proc/self/fdinfo/<fd> ("drm-engine-<class>: N ns").
 * Returns 0 and sets *ok=0 if the key is absent. */
static inline uint64_t fdinfo_value(int fd, const char *key, int *ok)
{
    char path[64], line[256];
    size_t klen = strlen(key);
    uint64_t v = 0;
    *ok = 0;
    snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", fd);
    FILE *f = fopen(path, "re");
    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, key, klen) && line[klen] == ':') {
            v = strtoull(line + klen + 1, NULL, 10);
            *ok = 1;
            break;
        }
    }
    fclose(f);
    return v;
}

struct engine_ns { uint64_t render, copy; int clients; };

/* Sum engine time over every open i915 render-node fd in this process, de-duplicated by
 * drm-client-id (a driver may dup its fd). Used for the Vulkan fixture, whose fd is internal. */
static inline struct engine_ns process_engine_ns(void)
{
    struct engine_ns e = {0};
    uint64_t seen[16];
    int nseen = 0;
    DIR *d = opendir("/proc/self/fd");
    if (!d)
        return e;
    struct dirent *de;
    while ((de = readdir(d))) {
        char link[64], target[PATH_MAX];
        int fd = atoi(de->d_name);
        if (de->d_name[0] == '.' || fd == dirfd(d))
            continue;
        snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
        ssize_t n = readlink(link, target, sizeof(target) - 1);
        if (n <= 0)
            continue;
        target[n] = 0;
        if (strncmp(target, "/dev/dri/renderD", 16))
            continue;
        int ok;
        uint64_t id = fdinfo_value(fd, "drm-client-id", &ok);
        if (!ok)
            continue;
        int dup = 0;
        for (int i = 0; i < nseen; i++)
            dup |= seen[i] == id;
        if (dup || nseen == 16)
            continue;
        seen[nseen++] = id;
        e.render += fdinfo_value(fd, "drm-engine-render", &ok);
        e.copy += fdinfo_value(fd, "drm-engine-copy", &ok);
        e.clients++;
    }
    closedir(d);
    return e;
}

/* Read-only GT actual frequency in MHz for the card behind this render-node fd, or -1. */
static inline int gt_act_freq_mhz(int fd)
{
    struct stat st;
    char dir[96], path[512];
    if (fstat(fd, &st) < 0 || !S_ISCHR(st.st_mode))
        return -1;
    snprintf(dir, sizeof(dir), "/sys/dev/char/%u:%u/device/drm", major(st.st_rdev),
             minor(st.st_rdev));
    DIR *d = opendir(dir);
    if (!d)
        return -1;
    int mhz = -1;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "card", 4) || strchr(de->d_name, '-'))
            continue;
        snprintf(path, sizeof(path), "%s/%s/gt_act_freq_mhz", dir, de->d_name);
        FILE *f = fopen(path, "re");
        if (f) {
            if (fscanf(f, "%d", &mhz) != 1)
                mhz = -1;
            fclose(f);
        }
        break;
    }
    closedir(d);
    return mhz;
}

/* "4K,64K,1M,8M+4K" -> bytes. Each size must be a nonzero multiple of 4 KiB and <= 256 MiB. */
static inline int parse_sizes(const char *arg, uint64_t *out, int max)
{
    int n = 0;
    char *copy = strdup(arg), *save = NULL;
    if (!copy)
        return -1;
    for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        uint64_t total = 0;
        char *p = tok;
        while (*p) {
            char *end;
            uint64_t v = strtoull(p, &end, 10);
            if (end == p)
                goto bad;
            if (*end == 'K' || *end == 'k') v <<= 10, end++;
            else if (*end == 'M' || *end == 'm') v <<= 20, end++;
            total += v;
            if (*end == '+') end++;
            else if (*end) goto bad;
            p = end;
        }
        if (!total || total % PAGE_BYTES || total > MAX_COPY_BYTES || n == max)
            goto bad;
        out[n++] = total;
    }
    free(copy);
    return n;
bad:
    free(copy);
    return -1;
}

#endif
