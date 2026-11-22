/* int8 cooperative-matrix multiply-add: exact check and GPU timing.
 *
 * usage: coopmat-bench SHADER.spv signed|unsigned [ITER] [RUNS]
 * Each 8-lane subgroup multiplies an 8x32 by a 32x8 int8 tile ITER times
 * (coopmat.comp).  On Gfx9 this needs INTEL_LOWER_DPAS=1, which lowers DPAS
 * in brw_lower_dpas.cpp.
 * Exit status 0: every result exact.  1: mismatch.  2: setup error.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define VK(call) do { VkResult rc_ = (call); if (rc_ != VK_SUCCESS) { \
    fprintf(stderr, "%s returned %d\n", #call, rc_); exit(2); } } while (0)

enum { GROUPS = 336, SG_PER_GROUP = 8, TILES = GROUPS * SG_PER_GROUP, NBUF = 3 };

static uint32_t rng = 0x9e3779b9u;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 3) { fprintf(stderr, "usage: %s shader.spv signed|unsigned [iter] [runs]\n", argv[0]); return 2; }
    const int is_signed = strcmp(argv[2], "signed") == 0;
    const uint32_t iter = argc > 3 ? (uint32_t)atoi(argv[3]) : 1024;
    const unsigned runs = argc > 4 ? (unsigned)atoi(argv[4]) : 15;
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
    printf("device %s, driver %s %s\n", props.properties.deviceName, p12.driverName, p12.driverInfo);

    VkPhysicalDeviceCooperativeMatrixFeaturesKHR cmf = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR };
    VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &cmf };
    VkPhysicalDeviceVulkan12Features f12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &f13 };
    VkPhysicalDeviceFeatures2 feats = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f12 };
    vkGetPhysicalDeviceFeatures2(phys, &feats);
    if (!cmf.cooperativeMatrix || !f12.shaderInt8 || !f12.storageBuffer8BitAccess ||
        !f13.subgroupSizeControl || !f13.computeFullSubgroups || !f12.vulkanMemoryModel) {
        fprintf(stderr, "missing cooperativeMatrix (set INTEL_LOWER_DPAS=1 on Gfx9), int8 or subgroup size control\n");
        return 2;
    }

    VkPhysicalDeviceCooperativeMatrixFeaturesKHR ecm = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR, .cooperativeMatrix = VK_TRUE };
    VkPhysicalDeviceVulkan13Features e13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &ecm, .subgroupSizeControl = VK_TRUE, .computeFullSubgroups = VK_TRUE };
    VkPhysicalDeviceVulkan12Features e12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &e13, .shaderInt8 = VK_TRUE, .storageBuffer8BitAccess = VK_TRUE, .vulkanMemoryModel = VK_TRUE };
    const char *exts[] = { VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME };
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &e12,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = exts };
    VkDevice dev;
    VK(vkCreateDevice(phys, &di, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    const VkDeviceSize sizes[NBUF] = { TILES * 256, TILES * 256, TILES * 64 * 4 };
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
        .requiredSubgroupSize = 8 };
    VkComputePipelineCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .pNext = &rs,
            .flags = VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sm, .pName = "main",
            .pSpecializationInfo = &spi },
        .layout = pl };
    VkPipeline pipe;
    VK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, NULL, &pipe));

    VkQueryPoolCreateInfo qpi = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 };
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

    int8_t *a = map[0], *b = map[1];
    int32_t *c = map[2];
    for (unsigned i = 0; i < TILES * 256; i++) {
        /* Signed: -8..7 covers negative products; unsigned: 0..15. */
        int va = (int)(next() % 16), vb = (int)(next() % 16);
        a[i] = (int8_t)(is_signed ? va - 8 : va);
        b[i] = (int8_t)(is_signed ? vb - 8 : vb);
    }

    uint64_t *ns = calloc(runs, sizeof(*ns));
    unsigned bad = 0;
    for (unsigned run = 0; run < runs; run++) {
        memset(c, 0x5a, TILES * 64 * 4);
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        VK(vkResetCommandPool(dev, cp, 0));
        VK(vkBeginCommandBuffer(cmd, &bi));
        vkCmdResetQueryPool(cmd, qp, 0, 2);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qp, 0);
        vkCmdDispatch(cmd, GROUPS, 1, 1);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 1);
        VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &mb, 0, NULL, 0, NULL);
        VK(vkEndCommandBuffer(cmd));
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
        VK(vkResetFences(dev, 1, &fence));
        VK(vkQueueSubmit(queue, 1, &si, fence));
        VK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 30000000000ull));
        uint64_t ts[2];
        VK(vkGetQueryPoolResults(dev, qp, 0, 2, sizeof(ts), ts, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
        ns[run] = (uint64_t)((double)(ts[1] - ts[0]) * props.properties.limits.timestampPeriod);

        if (run == 0) {
            for (unsigned t = 0; t < TILES; t++)
                for (unsigned i = 0; i < 8; i++)
                    for (unsigned j = 0; j < 8; j++) {
                        int64_t dot = 0;
                        for (unsigned k = 0; k < 32; k++) {
                            int64_t x = is_signed ? a[t * 256 + i * 32 + k] : (uint8_t)a[t * 256 + i * 32 + k];
                            int64_t y = is_signed ? b[t * 256 + k * 8 + j] : (uint8_t)b[t * 256 + k * 8 + j];
                            dot += x * y;
                        }
                        int32_t want = (int32_t)(dot * iter), got = c[t * 64 + i * 8 + j];
                        if (got != want) {
                            if (bad < 4)
                                printf("  tile %u [%u][%u]: got %d want %d\n", t, i, j, got, want);
                            bad++;
                        }
                    }
        }
    }
    qsort(ns, runs, sizeof(*ns), cmp_u64);
    const double macs = (double)TILES * 8 * 8 * 32 * iter;
    printf("%s int8, %u tiles x %u iterations: %u wrong of %u; GPU median %.3f ms (min %.3f, max %.3f), %.2f GMAC/s\n",
           is_signed ? "signed" : "unsigned", TILES, iter, bad, TILES * 64,
           ns[runs / 2] / 1e6, ns[0] / 1e6, ns[runs - 1] / 1e6, macs / ns[runs / 2]);
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
    return bad ? 1 : 0;
}
