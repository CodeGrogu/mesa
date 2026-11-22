/* Read-only i915 uAPI probes. An unavailable query is recorded by errno. */
#include <errno.h>
#include <fcntl.h>
#include <i915_drm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void *query(int fd, uint64_t id, const char *label)
{
    struct drm_i915_query_item item = { .query_id = id };
    struct drm_i915_query request = { .num_items = 1, .items_ptr = (uintptr_t)&item };
    int ret = ioctl(fd, DRM_IOCTL_I915_QUERY, &request);
    if (ret < 0 || item.length <= 0) {
        printf("%s: unavailable (%s)\n", label, strerror(ret < 0 ? errno : -item.length));
        return NULL;
    }

    void *data = calloc(1, item.length);
    if (!data) {
        perror("calloc");
        exit(1);
    }
    item.data_ptr = (uintptr_t)data;
    ret = ioctl(fd, DRM_IOCTL_I915_QUERY, &request);
    if (ret < 0 || item.length <= 0) {
        printf("%s: unavailable (%s)\n", label, strerror(ret < 0 ? errno : -item.length));
        free(data);
        return NULL;
    }
    printf("%s: %d bytes\n", label, item.length);
    return data;
}

int main(void)
{
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("renderD128");
        return 1;
    }
    const struct { int id; const char *name; } params[] = {
        { I915_PARAM_CHIPSET_ID, "PCI device" },
        { I915_PARAM_HUC_STATUS, "HuC authentication" },
        { I915_PARAM_HAS_EXEC_SOFTPIN, "Softpin" },
        { I915_PARAM_HAS_EXEC_TIMELINE_FENCES, "Timeline fences" },
        { I915_PARAM_HAS_CONTEXT_ISOLATION, "Context isolation" },
        { I915_PARAM_HAS_GPU_RESET, "GPU reset level" },
        { I915_PARAM_CS_TIMESTAMP_FREQUENCY, "CS timer frequency Hz" },
    };
    for (size_t i = 0; i < sizeof(params) / sizeof(params[0]); i++) {
        int val = 0;
        struct drm_i915_getparam param = { .param = params[i].id, .value = &val };
        if (ioctl(fd, DRM_IOCTL_I915_GETPARAM, &param) < 0)
            printf("%s: unavailable (%s)\n", params[i].name, strerror(errno));
        else
            printf("%s: %d\n", params[i].name, val);
    }

    struct drm_i915_query_engine_info *engines = query(fd, DRM_I915_QUERY_ENGINE_INFO, "Engine query");
    if (engines) {
        for (uint32_t i = 0; i < engines->num_engines; i++) {
            struct drm_i915_engine_info *engine = &engines->engines[i];
            printf("  class %u instance %u capabilities 0x%llx\n", engine->engine.engine_class,
                   engine->engine.engine_instance, (unsigned long long)engine->capabilities);
        }
        free(engines);
    }
    struct drm_i915_query_topology_info *topo = query(fd, DRM_I915_QUERY_TOPOLOGY_INFO, "Topology query");
    if (topo) {
        unsigned slices = 0, subslices = 0, eus = 0;
        for (unsigned s = 0; s < topo->max_slices; s++) {
            if (!(topo->data[s / 8] & (1u << (s % 8))))
                continue;
            slices++;
            for (unsigned sub = 0; sub < topo->max_subslices; sub++) {
                unsigned mask = topo->subslice_offset + s * topo->subslice_stride + sub / 8;
                if (!(topo->data[mask] & (1u << (sub % 8))))
                    continue;
                subslices++;
                unsigned start = topo->eu_offset + (s * topo->max_subslices + sub) * topo->eu_stride;
                for (unsigned eu = 0; eu < topo->max_eus_per_subslice; eu++)
                    eus += (topo->data[start + eu / 8] >> (eu % 8)) & 1u;
            }
        }
        printf("  %u slice(s), %u subslice(s), %u EUs\n", slices, subslices, eus);
        free(topo);
    }
    struct drm_i915_query_memory_regions *memory = query(fd, DRM_I915_QUERY_MEMORY_REGIONS, "Memory query");
    if (memory) {
        for (uint32_t i = 0; i < memory->num_regions; i++) {
            struct drm_i915_memory_region_info *region = &memory->regions[i];
            printf("  class %u instance %u probed %llu bytes\n", region->region.memory_class,
                   region->region.memory_instance, (unsigned long long)region->probed_size);
        }
        free(memory);
    }
    free(query(fd, DRM_I915_QUERY_HWCONFIG_BLOB, "GuC HWCONFIG query"));
    close(fd);
    return 0;
}
