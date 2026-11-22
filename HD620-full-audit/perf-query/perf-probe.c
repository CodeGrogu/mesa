/* VK_KHR_performance_query probe.
 *
 * usage: perf-probe SHADER.spv GROUPS ITER COUNTER_NAME [COUNTER_NAME...]
 * The shader has local_size_x = 32.  Lists the device's counters (with
 * PERF_LIST=1), brackets one dispatch of GROUPS workgroups with a
 * performance query on the named counters and prints each result.  If
 * "N compute shader invocations" is selected it must equal GROUPS * 32.
 * Exit status 0: results valid.  1: wrong value.  2: setup error/unsupported.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#define VK(call) do { VkResult rc_ = (call); if (rc_ != VK_SUCCESS) { \
    fprintf(stderr, "%s returned %d\n", #call, rc_); exit(2); } } while (0)

enum { NBUF = 3, BUF_BYTES = 1 << 20 };

static uint32_t rng = 0x9e3779b9u;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static int exit_status = 0;

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 5) { fprintf(stderr, "usage: %s shader.spv groups iter counter...\n", argv[0]); return 2; }
    const uint32_t groups = (uint32_t)atoi(argv[2]);
    const uint32_t iter = (uint32_t)atoi(argv[3]);
    const unsigned runs = 1;
    const uint32_t sg = 8;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static uint32_t code[1 << 14];
    size_t bytes = fread(code, 1, sizeof(code), f);
    fclose(f);
    if (bytes < 20 || bytes % 4 || bytes == sizeof(code)) { fprintf(stderr, "bad SPIR-V\n"); return 2; }

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    VK(vkCreateInstance(&ii, NULL, &inst));
    uint32_t count = 1;
    VkPhysicalDevice phys;
    VkResult er = vkEnumeratePhysicalDevices(inst, &count, &phys);
    if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || !count) { fprintf(stderr, "no device\n"); return 2; }

    VkPhysicalDeviceVulkan12Properties p12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
    VkPhysicalDeviceProperties2 props = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &p12 };
    vkGetPhysicalDeviceProperties2(phys, &props);

    VkPhysicalDeviceVulkan12Features e12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .hostQueryReset = VK_TRUE };
    VkPhysicalDevicePerformanceQueryFeaturesKHR pqf = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR,
        .pNext = &e12, .performanceCounterQueryPools = VK_TRUE };
    VkPhysicalDeviceShaderClockFeaturesKHR clk = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR, .pNext = &pqf,
        .shaderSubgroupClock = VK_TRUE };
    VkPhysicalDeviceVulkan13Features e13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &clk, .subgroupSizeControl = VK_TRUE, .computeFullSubgroups = VK_TRUE };
    const char *exts[] = { VK_KHR_SHADER_CLOCK_EXTENSION_NAME, VK_KHR_PERFORMANCE_QUERY_EXTENSION_NAME };

    /* Counters on queue family 0. */
    PFN_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR enum_counters =
        (void *)vkGetInstanceProcAddr(inst, "vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR");
    PFN_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR get_passes =
        (void *)vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR");
    if (!enum_counters || !get_passes) { fprintf(stderr, "VK_KHR_performance_query not available\n"); return 2; }
    uint32_t n_counters = 0;
    VK(enum_counters(phys, 0, &n_counters, NULL, NULL));
    VkPerformanceCounterKHR *counters = calloc(n_counters, sizeof(*counters));
    VkPerformanceCounterDescriptionKHR *descs = calloc(n_counters, sizeof(*descs));
    for (uint32_t i = 0; i < n_counters; i++) {
        counters[i].sType = VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_KHR;
        descs[i].sType = VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_DESCRIPTION_KHR;
    }
    VK(enum_counters(phys, 0, &n_counters, counters, descs));
    printf("%u counters\n", n_counters);
    if (getenv("PERF_LIST"))
        for (uint32_t i = 0; i < n_counters; i++)
            printf("  [%u] %s / %s\n", i, descs[i].category, descs[i].name);
    const unsigned n_sel = argc - 4;
    uint32_t sel[64];
    int cs_inv = -1;
    for (unsigned k = 0; k < n_sel; k++) {
        sel[k] = UINT32_MAX;
        for (uint32_t i = 0; i < n_counters; i++)
            if (!strcmp(descs[i].name, argv[4 + k])) { sel[k] = i; break; }
        if (sel[k] == UINT32_MAX) { fprintf(stderr, "no counter '%s'\n", argv[4 + k]); return 2; }
        if (!strcmp(argv[4 + k], "N compute shader invocations")) cs_inv = k;
    }
    VkQueryPoolPerformanceCreateInfoKHR pci_perf = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_PERFORMANCE_CREATE_INFO_KHR,
        .queueFamilyIndex = 0, .counterIndexCount = n_sel, .pCounterIndices = sel };
    uint32_t n_passes = 0;
    get_passes(phys, &pci_perf, &n_passes);
    printf("passes: %u\n", n_passes);
    if (n_passes == 0) return 2;
    VkPhysicalDeviceFeatures2 ef = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &e13 };
    ef.features.shaderFloat64 = VK_TRUE;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &ef,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi,
        .enabledExtensionCount = 2, .ppEnabledExtensionNames = exts };
    VkDevice dev;
    VK(vkCreateDevice(phys, &di, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    const VkDeviceSize sizes[NBUF] = { BUF_BYTES, BUF_BYTES, BUF_BYTES };
    VkBuffer buf[NBUF];
    VkDeviceMemory mem[NBUF];
    void *map[NBUF];
    for (int b = 0; b < NBUF; b++) {
        VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizes[b],
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
        VK(vkCreateBuffer(dev, &bi, NULL, &buf[b]));
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(dev, buf[b], &req);
        uint32_t want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, type = UINT32_MAX;
        for (uint32_t t = 0; t < mp.memoryTypeCount; t++)
            if ((req.memoryTypeBits & (1u << t)) && (mp.memoryTypes[t].propertyFlags & want) == want) { type = t; break; }
        if (type == UINT32_MAX) { fprintf(stderr, "no host-visible memory\n"); return 2; }
        VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = req.size, .memoryTypeIndex = type };
        VK(vkAllocateMemory(dev, &ai, NULL, &mem[b]));
        VK(vkBindBufferMemory(dev, buf[b], mem[b], 0));
        VK(vkMapMemory(dev, mem[b], 0, VK_WHOLE_SIZE, 0, &map[b]));
    }

    VkDescriptorSetLayoutBinding lb[NBUF];
    for (int b = 0; b < NBUF; b++)
        lb[b] = (VkDescriptorSetLayoutBinding){ b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL };
    VkDescriptorSetLayoutCreateInfo li = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = NBUF, .pBindings = lb };
    VkDescriptorSetLayout dsl;
    VK(vkCreateDescriptorSetLayout(dev, &li, NULL, &dsl));
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl };
    VkPipelineLayout pl;
    VK(vkCreatePipelineLayout(dev, &pli, NULL, &pl));
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NBUF };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
    VkDescriptorPool pool;
    VK(vkCreateDescriptorPool(dev, &dpi, NULL, &pool));
    VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &dsl };
    VkDescriptorSet set;
    VK(vkAllocateDescriptorSets(dev, &dai, &set));
    VkDescriptorBufferInfo dbi[NBUF];
    VkWriteDescriptorSet w[NBUF];
    for (int b = 0; b < NBUF; b++) {
        dbi[b] = (VkDescriptorBufferInfo){ buf[b], 0, VK_WHOLE_SIZE };
        w[b] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set,
            .dstBinding = b, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &dbi[b] };
    }
    vkUpdateDescriptorSets(dev, NBUF, w, 0, NULL);

    VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytes, .pCode = code };
    VkShaderModule sm;
    VK(vkCreateShaderModule(dev, &smi, NULL, &sm));
    VkSpecializationMapEntry sme = { 0, 0, 4 };
    VkSpecializationInfo spi = { 1, &sme, 4, &iter };
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO,
        .requiredSubgroupSize = sg };
    VkComputePipelineCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .pNext = &rs,
            .flags = VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sm, .pName = "main",
            .pSpecializationInfo = &spi },
        .layout = pl };
    VkPipeline pipe;
    VK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, NULL, &pipe));

    VkQueryPoolCreateInfo qpi = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .pNext = &pci_perf, .queryType = VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR, .queryCount = 1 };
    VkQueryPool qp;
    VK(vkCreateQueryPool(dev, &qpi, NULL, &qp));
    VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 };
    VkCommandPool cp;
    VK(vkCreateCommandPool(dev, &cpi, NULL, &cp));
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cp, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cmd;
    VK(vkAllocateCommandBuffers(dev, &cai, &cmd));
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    VK(vkCreateFence(dev, &fi, NULL, &fence));

    for (unsigned i = 0; i < BUF_BYTES / 4; i++) {
        ((float *)map[0])[i] = 1.0f;
        ((float *)map[1])[i] = 1.0f;
    }
    PFN_vkAcquireProfilingLockKHR acquire_lock = (void *)vkGetDeviceProcAddr(dev, "vkAcquireProfilingLockKHR");
    PFN_vkReleaseProfilingLockKHR release_lock = (void *)vkGetDeviceProcAddr(dev, "vkReleaseProfilingLockKHR");
    VkAcquireProfilingLockInfoKHR lock = { .sType = VK_STRUCTURE_TYPE_ACQUIRE_PROFILING_LOCK_INFO_KHR,
                                           .timeout = UINT64_MAX };
    VK(acquire_lock(dev, &lock));
    uint64_t *ns = calloc(runs, sizeof(*ns));
    uint64_t *cyc = calloc(runs, sizeof(*cyc));
    /* PACE_US: start one dispatch every PACE_US microseconds (frame pacing). */
    const char *pace_env = getenv("PACE_US");
    const long pace_us = pace_env ? atol(pace_env) : 0;
    struct timespec next_t;
    clock_gettime(CLOCK_MONOTONIC, &next_t);
    for (unsigned run = 0; run < runs; run++) {
        if (pace_us) {
            next_t.tv_nsec += pace_us * 1000;
            while (next_t.tv_nsec >= 1000000000L) { next_t.tv_nsec -= 1000000000L; next_t.tv_sec++; }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_t, NULL);
        }
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = 0 };
        vkResetQueryPool(dev, qp, 0, 1);
        VK(vkResetCommandPool(dev, cp, 0));
        VK(vkBeginCommandBuffer(cmd, &bi));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
        vkCmdBeginQuery(cmd, qp, 0, 0);
        vkCmdDispatch(cmd, groups, 1, 1);
        vkCmdEndQuery(cmd, qp, 0);
        VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &mb, 0, NULL, 0, NULL);
        VK(vkEndCommandBuffer(cmd));
        /* Every pass replays the same command buffer. */
        for (uint32_t pass = 0; pass < n_passes; pass++) {
            VkPerformanceQuerySubmitInfoKHR psi = { .sType = VK_STRUCTURE_TYPE_PERFORMANCE_QUERY_SUBMIT_INFO_KHR,
                                                    .counterPassIndex = pass };
            VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &psi,
                                .commandBufferCount = 1, .pCommandBuffers = &cmd };
            VK(vkResetFences(dev, 1, &fence));
            VK(vkQueueSubmit(queue, 1, &si, fence));
            VK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 30000000000ull));
        }
        VkPerformanceCounterResultKHR res[64];
        VK(vkGetQueryPoolResults(dev, qp, 0, 1, sizeof(res[0]) * n_sel, res, sizeof(res[0]) * n_sel,
                                 VK_QUERY_RESULT_WAIT_BIT));
        for (unsigned k = 0; k < n_sel; k++) {
            const VkPerformanceCounterKHR *ctr = &counters[sel[k]];
            if (ctr->storage == VK_PERFORMANCE_COUNTER_STORAGE_FLOAT32_KHR)
                printf("  %s = %g\n", descs[sel[k]].name, res[k].float32);
            else if (ctr->storage == VK_PERFORMANCE_COUNTER_STORAGE_FLOAT64_KHR)
                printf("  %s = %g\n", descs[sel[k]].name, res[k].float64);
            else if (ctr->storage == VK_PERFORMANCE_COUNTER_STORAGE_UINT32_KHR || ctr->storage == VK_PERFORMANCE_COUNTER_STORAGE_INT32_KHR)
                printf("  %s = %u\n", descs[sel[k]].name, res[k].uint32);
            else
                printf("  %s = %llu\n", descs[sel[k]].name, (unsigned long long)res[k].uint64);
        }
        if (cs_inv >= 0) {
            const uint64_t want = (uint64_t)groups * 32;
            printf("%s: compute invocations %llu, expected %llu\n",
                   res[cs_inv].uint64 == want ? "PASS" : "FAIL",
                   (unsigned long long)res[cs_inv].uint64, (unsigned long long)want);
            if (res[cs_inv].uint64 != want) exit_status = 1;
        }
        ns[run] = 0;
        cyc[run] = 0;

    }
    qsort(ns, runs, sizeof(*ns), cmp_u64);
    qsort(cyc, runs, sizeof(*cyc), cmp_u64);
    release_lock(dev);
    free(cyc);
    vkDestroyFence(dev, fence, NULL);
    vkDestroyCommandPool(dev, cp, NULL);
    vkDestroyQueryPool(dev, qp, NULL);
    vkDestroyPipeline(dev, pipe, NULL);
    vkDestroyShaderModule(dev, sm, NULL);
    vkDestroyDescriptorPool(dev, pool, NULL);
    vkDestroyPipelineLayout(dev, pl, NULL);
    vkDestroyDescriptorSetLayout(dev, dsl, NULL);
    for (int i = 0; i < NBUF; i++) { vkDestroyBuffer(dev, buf[i], NULL); vkFreeMemory(dev, mem[i], NULL); }
    free(ns);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);
    return exit_status;
}
