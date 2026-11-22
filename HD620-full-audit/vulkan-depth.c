/* Compare repeated, checked depth attachment clears with 1 or 4 allocated mips. */
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

#define VK(call) do { VkResult rc = (call); if (rc != VK_SUCCESS) { \
    fprintf(stderr, "%s returned %d\n", #call, rc); exit(1); } } while (0)

enum { width = 2048, clears = 32 };
static VkDevice device;
static VkPhysicalDeviceMemoryProperties types;

static VkDeviceMemory alloc(VkMemoryRequirements req, VkMemoryPropertyFlags flags)
{
    uint32_t i = 0;
    while (i < types.memoryTypeCount &&
           (!(req.memoryTypeBits & (1u << i)) || (types.memoryTypes[i].propertyFlags & flags) != flags))
        i++;
    if (i == types.memoryTypeCount) exit(1);
    VkMemoryAllocateInfo desc = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = i };
    VkDeviceMemory mem;
    VK(vkAllocateMemory(device, &desc, NULL, &mem));
    return mem;
}

int main(void)
{
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ic = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance instance;
    VK(vkCreateInstance(&ic, NULL, &instance));
    uint32_t count = 1;
    VkPhysicalDevice gpu;
    VK(vkEnumeratePhysicalDevices(instance, &count, &gpu));
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu, &props);
    if (props.vendorID != 0x8086 || props.deviceID != 0x5916) return 1;
    vkGetPhysicalDeviceMemoryProperties(gpu, &types);
    VkQueueFamilyProperties family[16];
    count = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, family);
    if (!count || !(family[0].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !family[0].timestampValidBits) return 1;
    printf("%s Vulkan driver %u timestamp period %.3f ns\n", props.deviceName,
           props.driverVersion, props.limits.timestampPeriod);

    const float priority = 1;
    VkDeviceQueueCreateInfo qc = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueCount = 1, .pQueuePriorities = &priority };
    VkPhysicalDeviceDynamicRenderingFeatures dr = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .dynamicRendering = VK_TRUE };
    VkDeviceCreateInfo dc = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &dr,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qc };
    VK(vkCreateDevice(gpu, &dc, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);
    VkCommandPoolCreateInfo cp = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandPool pool;
    VK(vkCreateCommandPool(device, &cp, NULL, &pool));
    VkCommandBufferAllocateInfo ca = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .commandBufferCount = 1 };
    VkCommandBuffer cmd;
    VK(vkAllocateCommandBuffers(device, &ca, &cmd));
    VkQueryPoolCreateInfo qp = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 };
    VkQueryPool timestamps;
    VK(vkCreateQueryPool(device, &qp, NULL, &timestamps));

    VkBufferCreateInfo bc = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    VkBuffer readback;
    VK(vkCreateBuffer(device, &bc, NULL, &readback));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, readback, &req);
    VkDeviceMemory read_mem = alloc(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkBindBufferMemory(device, readback, read_mem, 0));
    float *depth;
    VK(vkMapMemory(device, read_mem, 0, 4, 0, (void **)&depth));

    VkImage images[2];
    VkDeviceMemory backing[2];
    VkImageView views[2];
    const VkImageSubresourceRange range = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    for (unsigned i = 0; i < 2; i++) {
        VkImageCreateInfo image = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT, .extent = { width, width, 1 }, .mipLevels = i ? 4 : 1,
            .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
        VK(vkCreateImage(device, &image, NULL, &images[i]));
        vkGetImageMemoryRequirements(device, images[i], &req);
        backing[i] = alloc(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK(vkBindImageMemory(device, images[i], backing[i], 0));
        VkImageViewCreateInfo view = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = image.format,
            .subresourceRange = range };
        VK(vkCreateImageView(device, &view, NULL, &views[i]));
    }
    const VkPipelineStageFlags tests = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkImageLayout ds = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    for (unsigned trial = 0; trial < 9; trial++) {
        for (unsigned mode = 0; mode < 2; mode++) {
            unsigned i = (trial + mode) % 2;
            VK(vkResetCommandPool(device, pool, 0));
            VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            VK(vkBeginCommandBuffer(cmd, &begin));
            VkImageMemoryBarrier layout = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, .newLayout = ds,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = images[i], .subresourceRange = range };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, tests, 0, 0, NULL, 0, NULL, 1, &layout);
            vkCmdResetQueryPool(cmd, timestamps, 0, 2);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, timestamps, 0);
            VkRenderingAttachmentInfo depth_att = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = views[i], .imageLayout = ds, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
            VkRenderingInfo render = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = { .extent = { width, width } }, .layerCount = 1, .pDepthAttachment = &depth_att };
            layout.oldLayout = ds;
            layout.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            for (unsigned j = 0; j < clears; j++) {
                depth_att.clearValue.depthStencil.depth = (j + trial) % 2 ? 0.25f : 1.0f;
                vkCmdBeginRendering(cmd, &render);
                vkCmdEndRendering(cmd);
                vkCmdPipelineBarrier(cmd, tests, tests, 0, 0, NULL, 0, NULL, 1, &layout);
            }
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, timestamps, 1);
            layout.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            layout.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, tests, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &layout);
            VkBufferImageCopy region = { .imageSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 },
                .imageOffset = { width/2, width/2, 0 }, .imageExtent = { 1, 1, 1 } };
            vkCmdCopyImageToBuffer(cmd, images[i], layout.newLayout, readback, 1, &region);
            VkMemoryBarrier sync = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                                 0, 1, &sync, 0, NULL, 0, NULL);
            VK(vkEndCommandBuffer(cmd));
            VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1, .pCommandBuffers = &cmd };
            VK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
            VK(vkQueueWaitIdle(queue));
            uint64_t ticks[2];
            VK(vkGetQueryPoolResults(device, timestamps, 0, 2, sizeof(ticks), ticks, sizeof(ticks[0]),
                                     VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
            uint64_t mask = UINT64_MAX >> (64 - family[0].timestampValidBits);
            double ms = ((ticks[1] - ticks[0]) & mask) * props.limits.timestampPeriod / 1e6;
            if (*depth != depth_att.clearValue.depthStencil.depth) {
                fprintf(stderr, "depth %.3f expected %.3f\n", *depth, depth_att.clearValue.depthStencil.depth);
                return 2;
            }
            if (trial) printf("%u,%u,%.6f,%.2f\n", trial, i ? 4 : 1, ms, *depth);
        }
    }
    for (unsigned i = 0; i < 2; i++) {
        vkDestroyImageView(device, views[i], NULL);
        vkDestroyImage(device, images[i], NULL);
        vkFreeMemory(device, backing[i], NULL);
    }
    vkUnmapMemory(device, read_mem);
    vkDestroyBuffer(device, readback, NULL);
    vkFreeMemory(device, read_mem, NULL);
    vkDestroyQueryPool(device, timestamps, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
}
