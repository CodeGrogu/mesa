/* Check GEM, DMA-BUF, a one-command BCS batch and timeline sync on the render node. */
#include <fcntl.h>
#include <i915_drm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void call(int fd, unsigned long request, void *data, const char *name)
{
    if (ioctl(fd, request, data) < 0) {
        perror(name);
        exit(1);
    }
}
#define DRM(fd, request, data) call(fd, request, data, #request)

int main(void)
{
    int first = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    int second = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (first < 0 || second < 0) {
        perror("renderD128");
        return 1;
    }

    struct drm_i915_gem_create object = { .size = 4096 };
    DRM(first, DRM_IOCTL_I915_GEM_CREATE, &object);
    struct drm_prime_handle prime = { .handle = object.handle, .flags = DRM_CLOEXEC | DRM_RDWR };
    DRM(first, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime);
    struct drm_prime_handle imported = { .fd = prime.fd };
    DRM(second, DRM_IOCTL_PRIME_FD_TO_HANDLE, &imported);
    printf("GEM created and exported, imported on a separate DRM file\n");
    struct drm_gem_close drop = { .handle = imported.handle };
    DRM(second, DRM_IOCTL_GEM_CLOSE, &drop);
    close(prime.fd);
    close(second);

    const uint32_t batch[] = { 0x05000000, 0 }; /* MI_BATCH_BUFFER_END, pad */
    struct drm_i915_gem_pwrite write = { .handle = object.handle, .size = sizeof(batch),
        .data_ptr = (uintptr_t)batch };
    DRM(first, DRM_IOCTL_I915_GEM_PWRITE, &write);
    struct drm_i915_gem_exec_object2 exec_object = { .handle = object.handle };
    struct drm_i915_gem_execbuffer2 exec = { .buffers_ptr = (uintptr_t)&exec_object,
        .buffer_count = 1, .batch_len = sizeof(batch), .flags = I915_EXEC_BLT };
    DRM(first, DRM_IOCTL_I915_GEM_EXECBUFFER2, &exec);
    struct drm_i915_gem_wait wait = { .bo_handle = object.handle, .timeout_ns = 2000000000ll };
    DRM(first, DRM_IOCTL_I915_GEM_WAIT, &wait);
    printf("BCS accepts and completes a minimal batch\n");
    drop.handle = object.handle;
    DRM(first, DRM_IOCTL_GEM_CLOSE, &drop);

    struct drm_syncobj_create sync = {0};
    DRM(first, DRM_IOCTL_SYNCOBJ_CREATE, &sync);
    uint64_t point = 7;
    struct drm_syncobj_timeline_array timeline = { .handles = (uintptr_t)&sync.handle,
        .points = (uintptr_t)&point, .count_handles = 1 };
    DRM(first, DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL, &timeline);
    point = 0;
    DRM(first, DRM_IOCTL_SYNCOBJ_QUERY, &timeline);
    if (point != 7)
        return 2;
    struct drm_syncobj_destroy sync_drop = { .handle = sync.handle };
    DRM(first, DRM_IOCTL_SYNCOBJ_DESTROY, &sync_drop);
    printf("DRM timeline syncobj signaled and queried point 7\n");
    close(first);
    return 0;
}
