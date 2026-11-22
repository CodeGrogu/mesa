/* Compile, and optionally dispatch, a compute shader at a required subgroup
 * size, for the Gfx9 hang checks.
 *
 * usage: hang-compute SHADER.spv SUBGROUP_SIZE
 *   DISPATCH=1   dispatch (otherwise compile only), wait up to 30 s
 *   REPEAT=n     number of submissions (default 20)
 * Buffers 0..3 are 4 MiB storage buffers of FP16 values in [-2, 2].
 * Exit 0 when all submissions complete, 4 on VK_ERROR_DEVICE_LOST or a
 * timeout, 2 on setup failure.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define VK(call) do { VkResult rc_ = (call); if (rc_ != VK_SUCCESS) { \
    fprintf(stderr, "%s:%d %s returned %d\n", __FILE__, __LINE__, #call, rc_); \
    exit(rc_ == VK_ERROR_DEVICE_LOST ? 4 : 2); } } while (0)

enum { NBUF = 4, BYTES = 4 << 20, GROUPS = 256 };

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static uint32_t code[1 << 14];
    size_t bytes = fread(code, 1, sizeof(code), f);
    fclose(f);
    const uint32_t sg = atoi(argv[2]);
    const int dispatch = getenv("DISPATCH") && atoi(getenv("DISPATCH"));
    const int repeat = getenv("REPEAT") ? atoi(getenv("REPEAT")) : 20;

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    VK(vkCreateInstance(&ii, NULL, &inst));
    uint32_t n = 1;
    VkPhysicalDevice phys;
    VkResult er = vkEnumeratePhysicalDevices(inst, &n, &phys);
    if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || !n) return 2;
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);

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
    VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = bytes, .pCode = code };
    VkShaderModule sm;
    VK(vkCreateShaderModule(dev, &smi, NULL, &sm));
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO, .requiredSubgroupSize = sg };
    VkComputePipelineCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .pNext = &rs,
                   .flags = VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT,
                   .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sm, .pName = "main" }, .layout = pl };
    VkPipeline pipe;
    VK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, NULL, &pipe));
    printf("pipeline created at subgroup size %u\n", sg);
    if (!dispatch)
        return 0;

    VkBuffer buf[NBUF];
    for (int b = 0; b < NBUF; b++) {
        VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = BYTES,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
        VK(vkCreateBuffer(dev, &bi, NULL, &buf[b]));
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(dev, buf[b], &req);
        const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        uint32_t t = 0;
        while (t < mp.memoryTypeCount && !((req.memoryTypeBits & (1u << t)) && (mp.memoryTypes[t].propertyFlags & want) == want))
            t++;
        VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = t };
        VkDeviceMemory m;
        VK(vkAllocateMemory(dev, &ai, NULL, &m));
        VK(vkBindBufferMemory(dev, buf[b], m, 0));
        uint16_t *map;
        VK(vkMapMemory(dev, m, 0, VK_WHOLE_SIZE, 0, (void **)&map));
        uint32_t rng = 0x2545f491u * (b + 1);
        for (int i = 0; i < BYTES / 2; i++) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            /* Half values in [-2, 2): sign, exponent 14..15, random mantissa. */
            map[i] = (uint16_t)(((rng >> 31) << 15) | ((14 + ((rng >> 20) & 1)) << 10) | (rng & 0x3ff));
        }
    }
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NBUF };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
    VkDescriptorPool dpool;
    VK(vkCreateDescriptorPool(dev, &dpi, NULL, &dpool));
    VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dpool, .descriptorSetCount = 1, .pSetLayouts = &dsl };
    VkDescriptorSet set;
    VK(vkAllocateDescriptorSets(dev, &dai, &set));
    VkDescriptorBufferInfo dbi[NBUF];
    VkWriteDescriptorSet w[NBUF];
    for (int b = 0; b < NBUF; b++) {
        dbi[b] = (VkDescriptorBufferInfo){ buf[b], 0, VK_WHOLE_SIZE };
        w[b] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = b,
            .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi[b] };
    }
    vkUpdateDescriptorSets(dev, NBUF, w, 0, NULL);

    VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandPool cp;
    VK(vkCreateCommandPool(dev, &cpi, NULL, &cp));
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cp,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cb;
    VK(vkAllocateCommandBuffers(dev, &cai, &cb));
    VkCommandBufferBeginInfo cbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VK(vkBeginCommandBuffer(cb, &cbi));
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
    vkCmdDispatch(cb, GROUPS, 1, 1);
    VK(vkEndCommandBuffer(cb));
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    VK(vkCreateFence(dev, &fci, NULL, &fence));
    for (int r = 0; r < repeat; r++) {
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
        VK(vkResetFences(dev, 1, &fence));
        VK(vkQueueSubmit(queue, 1, &si, fence));
        VkResult wr = vkWaitForFences(dev, 1, &fence, VK_TRUE, 30000000000ull);
        if (wr != VK_SUCCESS) {
            printf("submission %d: %s (%d)\n", r, wr == VK_ERROR_DEVICE_LOST ? "VK_ERROR_DEVICE_LOST" :
                   wr == VK_TIMEOUT ? "VK_TIMEOUT" : "error", wr);
            return 4;
        }
    }
    printf("%d submissions completed\n", repeat);
    /* Exit without teardown: the process exit releases everything. */
    return 0;
}
