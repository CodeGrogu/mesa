/* Exact check of FP16 multiply-add under divergent control flow at each
 * required compute subgroup size (Mesa issue #16528).
 *
 * usage: lanemask SHADER.spv
 * Exit status 0: every lane matched at every supported size.
 * Exit status 1: at least one lane mismatched.  Exit status 2: setup error.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define VK(call) do { VkResult rc_ = (call); if (rc_ != VK_SUCCESS) { \
    fprintf(stderr, "%s returned %d\n", #call, rc_); exit(2); } } while (0)

enum { N = 1 << 16, GROUP = 128, NBUF = 7 };

static uint16_t f2h(float f)
{
    /* Inputs are small integers and halves, all exactly representable. */
    union { float f; uint32_t u; } v = { f };
    uint32_t sign = (v.u >> 16) & 0x8000, exp = (v.u >> 23) & 0xff, man = v.u & 0x7fffff;
    if (f == 0.0f) return sign;
    return sign | ((exp - 127 + 15) << 10) | (man >> 13);
}

static float h2f(uint16_t h)
{
    uint32_t sign = (h & 0x8000u) << 16, exp = (h >> 10) & 0x1f, man = h & 0x3ff;
    union { uint32_t u; float f; } v;
    if (exp == 0 && man == 0) v.u = sign;
    else v.u = sign | ((exp - 15 + 127) << 23) | (man << 13);
    return v.f;
}

static uint32_t rng = 0x9e3779b9u;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* Mask patterns: random, alternating 8-lane groups (lane L and L-8 differ),
 * single active lane per 32, and all-but-one active. */
static uint32_t mask_for(unsigned pattern, unsigned i)
{
    switch (pattern) {
    case 0: return next() & 1;
    case 1: return (i >> 3) & 1;
    case 2: return (i % 32) == 21;
    default: return (i % 32) != 13;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: %s shader.spv\n", argv[0]); return 2; }
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

    VkPhysicalDeviceVulkan13Properties p13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES };
    VkPhysicalDeviceVulkan12Properties p12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES, .pNext = &p13 };
    VkPhysicalDeviceProperties2 props = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &p12 };
    vkGetPhysicalDeviceProperties2(phys, &props);
    printf("device %s, driver %s %s\n", props.properties.deviceName, p12.driverName, p12.driverInfo);

    VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan12Features f12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &f13 };
    VkPhysicalDeviceVulkan11Features f11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &f12 };
    VkPhysicalDeviceFeatures2 feats = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f11 };
    vkGetPhysicalDeviceFeatures2(phys, &feats);
    if (!f12.shaderFloat16 || !f11.storageBuffer16BitAccess || !f13.subgroupSizeControl || !f13.computeFullSubgroups) {
        fprintf(stderr, "missing float16/16-bit storage/subgroup size control\n");
        return 2;
    }

    VkPhysicalDeviceVulkan13Features e13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .subgroupSizeControl = VK_TRUE, .computeFullSubgroups = VK_TRUE };
    VkPhysicalDeviceVulkan12Features e12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &e13, .shaderFloat16 = VK_TRUE };
    VkPhysicalDeviceVulkan11Features e11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &e12, .storageBuffer16BitAccess = VK_TRUE };
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &e11,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi };
    VkDevice dev;
    VK(vkCreateDevice(phys, &di, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    const VkDeviceSize sizes[NBUF] = { N * 2, N * 2, N * 2, N * 4, N * 2, N * 4, 4 };
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

    uint16_t *a = map[0], *b = map[1], *c = map[2], *o = map[4];
    uint32_t *m = map[3], *taken = map[5], *sgs = map[6];
    const uint32_t req_sizes[] = { 8, 16, 32 };
    unsigned total_bad = 0, ran = 0;

    for (unsigned s = 0; s < 3; s++) {
        uint32_t size = req_sizes[s];
        if (size < p13.minSubgroupSize || size > p13.maxSubgroupSize ||
            !(p13.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT))
            continue;
        VkPipelineShaderStageRequiredSubgroupSizeCreateInfo rs = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO,
            .requiredSubgroupSize = size };
        VkComputePipelineCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .pNext = &rs,
                .flags = VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sm, .pName = "main" },
            .layout = pl };
        VkPipeline pipe;
        VK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, NULL, &pipe));

        for (unsigned pattern = 0; pattern < 4; pattern++) {
            for (unsigned i = 0; i < N; i++) {
                a[i] = f2h((float)(int)(next() % 9) - 4.0f);
                b[i] = f2h(0.5f * (float)(int)(next() % 7) - 1.5f);
                c[i] = f2h((float)(int)(next() % 17) - 8.0f);
                m[i] = mask_for(pattern, i);
            }
            memset(o, 0xff, N * 2);
            memset(taken, 0, N * 4);
            *sgs = 0;

            VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
            VK(vkResetCommandPool(dev, cp, 0));
            VK(vkBeginCommandBuffer(cmd, &bi));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
            vkCmdDispatch(cmd, N / GROUP, 1, 1);
            VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                                 0, 1, &mb, 0, NULL, 0, NULL);
            VK(vkEndCommandBuffer(cmd));
            VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
            VK(vkResetFences(dev, 1, &fence));
            VK(vkQueueSubmit(queue, 1, &si, fence));
            VK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull));

            unsigned bad = 0, bad_taken = 0, lane_hist[32] = { 0 };
            for (unsigned i = 0; i < N; i++) {
                float av = h2f(a[i]), bv = h2f(b[i]), r = h2f(c[i]);
                if (m[i]) { r = fmaf(av, bv, r); r = fmaf(r, bv, av); }
                uint16_t want = f2h(r);
                if (o[i] != want && !(r == 0.0f && (o[i] & 0x7fff) == 0)) {
                    if (bad < 4)
                        printf("    i=%u lane=%u active=%u got %g want %g\n", i, i % 32, m[i], h2f(o[i]), r);
                    bad++;
                    lane_hist[i % 32]++;
                }
                if (taken[i] != m[i]) bad_taken++;
            }
            printf("required %2u (shader saw %2u) pattern %u: %6u wrong values, %u wrong branch flags\n",
                   size, *sgs, pattern, bad, bad_taken);
            if (bad) {
                printf("    wrong values by lane mod 32:");
                for (int l = 0; l < 32; l++) if (lane_hist[l]) printf(" %d:%u", l, lane_hist[l]);
                printf("\n");
            }
            total_bad += bad + bad_taken;
            ran++;
        }
        vkDestroyPipeline(dev, pipe, NULL);
    }
    printf("%s: %u mismatches across %u runs\n", total_bad ? "FAIL" : "PASS", total_bad, ran);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);
    return total_bad ? 1 : 0;
}
