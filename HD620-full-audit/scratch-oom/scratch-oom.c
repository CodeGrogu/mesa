/* Create a compute pipeline that needs scratch and report the result.
 * Pair with gemfail.so to inject a failure of the scratch BO allocation.
 * Never dispatches: an unfixed driver would point scratch at address 0.
 *
 * usage: scratch-oom spill.spv
 * Prints the vkCreateComputePipelines result; exit 0 on VK_SUCCESS,
 * 3 on VK_ERROR_OUT_OF_DEVICE_MEMORY, 2 on other setup failures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#define VK(call) do { VkResult rc_ = (call); if (rc_ != VK_SUCCESS) { \
    fprintf(stderr, "%s returned %d\n", #call, rc_); return 2; } } while (0)

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static uint32_t code[1 << 14];
    size_t bytes = fread(code, 1, sizeof(code), f);
    fclose(f);
    if (bytes < 20 || bytes % 4) return 2;

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    VK(vkCreateInstance(&ii, NULL, &inst));
    uint32_t n = 1;
    VkPhysicalDevice phys;
    VkResult er = vkEnumeratePhysicalDevices(inst, &n, &phys);
    if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || !n) return 2;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi };
    VkDevice dev;
    VK(vkCreateDevice(phys, &di, NULL, &dev));

    VkDescriptorSetLayoutBinding lb = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL };
    VkDescriptorSetLayoutCreateInfo li = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &lb };
    VkDescriptorSetLayout dsl;
    VK(vkCreateDescriptorSetLayout(dev, &li, NULL, &dsl));
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl };
    VkPipelineLayout pl;
    VK(vkCreatePipelineLayout(dev, &pli, NULL, &pl));
    VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = bytes, .pCode = code };
    VkShaderModule sm;
    VK(vkCreateShaderModule(dev, &smi, NULL, &sm));
    VkComputePipelineCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                   .module = sm, .pName = "main" }, .layout = pl };
    VkPipeline pipe = VK_NULL_HANDLE;
    VkResult r = vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, NULL, &pipe);
    printf("vkCreateComputePipelines = %d (%s)\n", r,
           r == VK_SUCCESS ? "VK_SUCCESS" :
           r == VK_ERROR_OUT_OF_DEVICE_MEMORY ? "VK_ERROR_OUT_OF_DEVICE_MEMORY" : "other");
    if (pipe && getenv("SCRATCH_DISPATCH")) {
        /* Correctness of the scratch-using shader (fixed driver, no injection). */
        enum { N = 4096 };
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(phys, &mp);
        VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = N * 4,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
        VkBuffer buf; VK(vkCreateBuffer(dev, &bi, NULL, &buf));
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev, buf, &req);
        uint32_t t = 0;
        for (; t < mp.memoryTypeCount; t++)
            if ((req.memoryTypeBits & (1u << t)) && (mp.memoryTypes[t].propertyFlags &
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) break;
        VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = t };
        VkDeviceMemory mem; VK(vkAllocateMemory(dev, &ai, NULL, &mem));
        VK(vkBindBufferMemory(dev, buf, mem, 0));
        uint32_t *v; VK(vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, (void **)&v));
        for (uint32_t i = 0; i < N; i++) v[i] = i * 7919u;
        VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
        VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
        VkDescriptorPool dp; VK(vkCreateDescriptorPool(dev, &dpi, NULL, &dp));
        VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = dp, .descriptorSetCount = 1, .pSetLayouts = &dsl };
        VkDescriptorSet set; VK(vkAllocateDescriptorSets(dev, &dai, &set));
        VkDescriptorBufferInfo dbi = { buf, 0, VK_WHOLE_SIZE };
        VkWriteDescriptorSet w = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0,
            .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi };
        vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
        VkQueue q; vkGetDeviceQueue(dev, 0, 0, &q);
        VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        VkCommandPool cp; VK(vkCreateCommandPool(dev, &cpi, NULL, &cp));
        VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cp,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
        VkCommandBuffer cb; VK(vkAllocateCommandBuffers(dev, &cai, &cb));
        VkCommandBufferBeginInfo cbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VK(vkBeginCommandBuffer(cb, &cbi));
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
        vkCmdDispatch(cb, N / 64, 1, 1);
        VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
        VK(vkEndCommandBuffer(cb));
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
        VK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE));
        VK(vkQueueWaitIdle(q));
        unsigned bad = 0;
        for (uint32_t i = 0; i < N; i++) {
            uint32_t in = i * 7919u, acc = 0;
            for (uint32_t k = 0; k < 64; k++) {
                uint32_t idx = (in + k * 97u) & 4095u;
                acc += idx * 2654435761u + i;
            }
            if (v[i] != acc) bad++;
        }
        printf("scratch dispatch: %u/%u wrong\n", bad, (unsigned)N);
        vkDestroyCommandPool(dev, cp, NULL);
        vkDestroyDescriptorPool(dev, dp, NULL);
        vkDestroyBuffer(dev, buf, NULL);
        vkFreeMemory(dev, mem, NULL);
        if (bad) r = VK_ERROR_UNKNOWN;
    }
    if (pipe) vkDestroyPipeline(dev, pipe, NULL);
    vkDestroyShaderModule(dev, sm, NULL);
    vkDestroyPipelineLayout(dev, pl, NULL);
    vkDestroyDescriptorSetLayout(dev, dsl, NULL);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);
    return r == VK_SUCCESS ? 0 : r == VK_ERROR_OUT_OF_DEVICE_MEMORY ? 3 : 2;
}
