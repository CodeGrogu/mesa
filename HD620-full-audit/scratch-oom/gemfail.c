/* LD_PRELOAD shim: fail i915 GEM buffer creation at or above a size.
 *
 *   GEMFAIL_MIN_BYTES=<n>   fail GEM_CREATE / GEM_CREATE_EXT with size >= n
 *   GEMFAIL_LOG=1           log every GEM create size to stderr
 *
 * Failures return -1 with errno ENOMEM, as the kernel would.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <drm/i915_drm.h>

int ioctl(int fd, unsigned long request, ...)
{
    static int (*real)(int, unsigned long, ...);
    if (!real)
        real = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    uint64_t size = 0;
    int is_create = 0;
    if (request == DRM_IOCTL_I915_GEM_CREATE) {
        size = ((struct drm_i915_gem_create *)arg)->size;
        is_create = 1;
    } else if (request == DRM_IOCTL_I915_GEM_CREATE_EXT) {
        size = ((struct drm_i915_gem_create_ext *)arg)->size;
        is_create = 1;
    }
    if (is_create) {
        const char *min = getenv("GEMFAIL_MIN_BYTES");
        int fail = min && size >= strtoull(min, NULL, 0);
        if (getenv("GEMFAIL_LOG"))
            fprintf(stderr, "gemfail: create %llu bytes%s\n",
                    (unsigned long long)size, fail ? " -> injected ENOMEM" : "");
        if (fail) {
            errno = ENOMEM;
            return -1;
        }
    }
    return real(fd, request, arg);
}
