/* Deterministic SPIR-V execution, GPU transfer and submit/wait amortization. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <vulkan/vulkan.h>

#define VK(call) do { VkResult rc = (call); if (rc != VK_SUCCESS) { \
    fprintf(stderr, "%s returned %d\n", #call, rc); exit(1); } } while (0)

enum { elements = 1 << 20, chunks = 64 };
static VkDevice device;
static VkQueue queue;
static VkCommandPool commands;
static VkCommandBuffer cmd;
static VkFence fence;

static uint64_t now_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return 1000000000ull * now.tv_sec + now.tv_nsec;
}

static void begin(void)
{
    VK(vkResetCommandPool(device, commands, 0));
    VkCommandBufferBeginInfo info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    VK(vkBeginCommandBuffer(cmd, &info));
}

static void execute(VkAccessFlags writes, VkPipelineStageFlags producer)
{
    VkMemoryBarrier host = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = writes, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    vkCmdPipelineBarrier(cmd, producer, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0, NULL);
    VK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &cmd };
    VK(vkResetFences(device, 1, &fence));
    VK(vkQueueSubmit(queue, 1, &submit, fence));
    VK(vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ull));
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    FILE *input = fopen(argv[1], "rb");
    uint32_t spirv[4096];
    if (!input) { perror(argv[1]); return 1; }
    size_t bytes = fread(spirv, 1, sizeof(spirv), input);
    if (!feof(input) || bytes < 20 || bytes % 4) return 1;
    fclose(input);

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo init = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app };
    VkInstance instance;
    VK(vkCreateInstance(&init, NULL, &instance));
    uint32_t count = 1;
    VkPhysicalDevice gpu;
    VK(vkEnumeratePhysicalDevices(instance, &count, &gpu));
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu, &props);
    if (props.vendorID != 0x8086 || props.deviceID != 0x5916) return 1;
    printf("%s driverVersion %u\n", props.deviceName, props.driverVersion);
    VkQueueFamilyProperties families[16];
    count = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families);
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) family++;
    if (family == count) return 1;
    printf("Using family %u flags=0x%x\n", family, families[family].queueFlags);
    const float priority = 1;
    VkDeviceQueueCreateInfo qc = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo dc = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qc };
    VK(vkCreateDevice(gpu, &dc, NULL, &device));
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo cp = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family };
    VK(vkCreateCommandPool(device, &cp, NULL, &commands));
    VkCommandBufferAllocateInfo ca = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commands, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VK(vkAllocateCommandBuffers(device, &ca, &cmd));
    VkFenceCreateInfo fc = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VK(vkCreateFence(device, &fc, NULL, &fence));

    const VkDeviceSize size = elements * sizeof(uint32_t);
    VkBufferCreateInfo bc = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    VkBuffer buf;
    VK(vkCreateBuffer(device, &bc, NULL, &buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buf, &req);
    VkPhysicalDeviceMemoryProperties memprops;
    vkGetPhysicalDeviceMemoryProperties(gpu, &memprops);
    VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = 0;
    while (type < memprops.memoryTypeCount &&
           (!(req.memoryTypeBits & (1u << type)) || (memprops.memoryTypes[type].propertyFlags & wanted) != wanted))
        type++;
    if (type == memprops.memoryTypeCount) return 1;
    VkMemoryAllocateInfo ma = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = type };
    VkDeviceMemory mem;
    uint32_t *mapped;
    VK(vkAllocateMemory(device, &ma, NULL, &mem));
    VK(vkBindBufferMemory(device, buf, mem, 0));
    VK(vkMapMemory(device, mem, 0, size, 0, (void **)&mapped));

    for (int trial = 0; trial < 9; trial++) {
        for (unsigned mode = 0; mode < 2; mode++) {
            unsigned batch = ((trial + mode) % 2 == 0) ? chunks : 1;
            uint64_t start = now_ns();
            for (unsigned i = 0; i < chunks; i += batch) {
                begin();
                for (unsigned j = i; j < i + batch; j++)
                    vkCmdFillBuffer(cmd, buf, j * size / chunks, size / chunks, j + trial + 1);
                execute(VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            }
            double ms = (now_ns() - start) / 1e6;
            for (unsigned i = 0; i < elements; i++)
                if (mapped[i] != i / (elements / chunks) + trial + 1) return 2;
            if (trial) printf("fill,%d,%u,%.4f\n", trial, batch, ms);
        }
    }

    VkDescriptorSetLayoutBinding binding = { .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
    VkDescriptorSetLayoutCreateInfo lc = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding };
    VkDescriptorSetLayout layout;
    VK(vkCreateDescriptorSetLayout(device, &lc, NULL, &layout));
    VkDescriptorPoolSize ds = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
    VkDescriptorPoolCreateInfo dp = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ds };
    VkDescriptorPool pool;
    VK(vkCreateDescriptorPool(device, &dp, NULL, &pool));
    VkDescriptorSetAllocateInfo da = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &layout };
    VkDescriptorSet set;
    VK(vkAllocateDescriptorSets(device, &da, &set));
    VkDescriptorBufferInfo db = { .buffer = buf, .range = size };
    VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &db };
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
    VkPipelineLayoutCreateInfo pl = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &layout };
    VkPipelineLayout pipeline_layout;
    VK(vkCreatePipelineLayout(device, &pl, NULL, &pipeline_layout));
    VkShaderModuleCreateInfo mc = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytes, .pCode = spirv };
    VkShaderModule module;
    VK(vkCreateShaderModule(device, &mc, NULL, &module));
    VkComputePipelineCreateInfo pc = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main" },
        .layout = pipeline_layout };
    VkPipeline pipeline;
    VK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pc, NULL, &pipeline));
    begin();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &set, 0, NULL);
    vkCmdDispatch(cmd, elements / 64, 1, 1);
    execute(VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    for (unsigned i = 0; i < elements; i++)
        if (mapped[i] != i * 7 + 3) return 3;
    printf("compute: %d exact values\n", elements);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyShaderModule(device, module, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyDescriptorPool(device, pool, NULL);
    vkDestroyDescriptorSetLayout(device, layout, NULL);
    vkUnmapMemory(device, mem);
    vkDestroyBuffer(device, buf, NULL);
    vkFreeMemory(device, mem, NULL);
    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, commands, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
