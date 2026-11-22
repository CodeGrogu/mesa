#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <time.h>
#include <xf86drm.h>
#include "drm-uapi/i915_drm.h"
int main(void) {
   int fd = open("/dev/dri/renderD128", O_RDWR);
   struct drm_i915_reg_read r = { .offset = 0x2358 | 1 /* I915_REG_READ_8B_WA */ };
   int ok = drmIoctl(fd, DRM_IOCTL_I915_REG_READ, &r);
   int f = 0; struct drm_i915_getparam gp = { .param = I915_PARAM_CS_TIMESTAMP_FREQUENCY, .value = &f };
   drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
   struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
   uint64_t raw = r.val & ((1ull << 36) - 1);
   printf("ioctl=%d raw=0x%llx bits_above_36=0x%llx freq=%d now=%ld seconds_to_wrap=%.1f\n", ok,
          (unsigned long long)r.val, (unsigned long long)(r.val >> 36), f, (long)ts.tv_sec,
          ((1ull << 36) - raw) / (double)f);
   return 0;
}
