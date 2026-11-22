/* Verify transfer-to-depth-attachment transitions at every allocated Gen9 mip. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

#define VK(call) do { VkResult rc = (call); if (rc != VK_SUCCESS) { \
    fprintf(stderr, "%s returned %d\n", #call, rc); exit(1); } } while (0)

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

    const struct { unsigned width, height, levels; } cases[] = {
        { 2048, 2048, 4 }, { 704, 288, 4 }, { 696, 288, 4 }, { 704, 280, 4 }, { 32, 32, 5 },
    };
    const VkPipelineStageFlags tests = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkAccessFlags depth_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        unsigned levels = cases[i].levels;
        VkImageCreateInfo image_info = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_D32_SFLOAT,
            .extent = { cases[i].width, cases[i].height, 1 }, .mipLevels = levels, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT };
        VkImage image;
        VK(vkCreateImage(device, &image_info, NULL, &image));
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device, image, &req);
        VkDeviceSize image_bytes = req.size;
        VkDeviceMemory image_mem = alloc(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK(vkBindImageMemory(device, image, image_mem, 0));

        VkImageView views[5];
        VkBufferImageCopy regions[5];
        VkDeviceSize total = 0;
        for (unsigned lod = 0; lod < levels; lod++) {
            const unsigned w = cases[i].width >> lod, h = cases[i].height >> lod;
            VkImageViewCreateInfo view = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = image_info.format,
                .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, lod, 1, 0, 1 } };
            VK(vkCreateImageView(device, &view, NULL, &views[lod]));
            regions[lod] = (VkBufferImageCopy){ .bufferOffset = total, .imageExtent = { w, h, 1 },
                .imageSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, lod, 0, 1 } };
            total += (VkDeviceSize)w * h * sizeof(float);
        }

        VkBufferCreateInfo buffer_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 2 * total,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT };
        VkBuffer staging;
        VK(vkCreateBuffer(device, &buffer_info, NULL, &staging));
        vkGetBufferMemoryRequirements(device, staging, &req);
        VkDeviceMemory staging_mem = alloc(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK(vkBindBufferMemory(device, staging, staging_mem, 0));
        float *mapped;
        VK(vkMapMemory(device, staging_mem, 0, 2 * total, 0, (void **)&mapped));

        for (unsigned reverse = 0; reverse < 2; reverse++) {
            for (unsigned lod = 0; lod < levels; lod++) {
                const unsigned w = regions[lod].imageExtent.width, h = regions[lod].imageExtent.height;
                float *pixels = mapped + regions[lod].bufferOffset / sizeof(float);
                for (unsigned y = 0; y < h; y++)
                    for (unsigned x = 0; x < w; x++)
                        pixels[y * w + x] = (float)(1 + (x + 7 * y + 53 * lod + 19 * reverse) % 511) / 512.0f;
            }
            memset((char *)mapped + total, 0, total);

            VK(vkResetCommandPool(device, pool, 0));
            VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            VK(vkBeginCommandBuffer(cmd, &begin));
            VkImageMemoryBarrier transition = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .image = image, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, levels, 0, 1 } };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, NULL, 0, NULL, 1, &transition);
            vkCmdCopyBufferToImage(cmd, staging, image, transition.newLayout, levels, regions);

            for (unsigned order = 0; order < levels; order++) {
                unsigned lod = reverse ? levels - 1 - order : order;
                const unsigned w = regions[lod].imageExtent.width, h = regions[lod].imageExtent.height;
                transition.subresourceRange.baseMipLevel = lod;
                transition.subresourceRange.levelCount = 1;
                transition.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                transition.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                transition.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                transition.dstAccessMask = depth_access;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, tests, 0, 0, NULL, 0, NULL, 1, &transition);
                VkRenderingAttachmentInfo depth = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = views[lod], .imageLayout = transition.newLayout, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
                VkRenderingInfo render = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = { .extent = { w, h } }, .layerCount = 1, .pDepthAttachment = &depth };
                float value = (float)(2 + lod + reverse) / 16.0f;
                VkClearAttachment attachment = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .clearValue.depthStencil = { value, 0 } };
                VkClearRect rect = { .rect = { { w / 4, h / 4 }, { w / 2, h / 2 } }, .layerCount = 1 };
                vkCmdBeginRendering(cmd, &render);
                vkCmdClearAttachments(cmd, 1, &attachment, 1, &rect);
                vkCmdEndRendering(cmd);

                transition.oldLayout = transition.newLayout;
                transition.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                transition.srcAccessMask = depth_access;
                transition.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd, tests, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &transition);
            }
            /* Copy only after touching every mip to catch writes into nearby mips. */
            for (unsigned lod = 0; lod < levels; lod++) {
                VkBufferImageCopy readback = regions[lod];
                readback.bufferOffset += total;
                vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &readback);
            }
            VkMemoryBarrier sync = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                                 0, 1, &sync, 0, NULL, 0, NULL);
            VK(vkEndCommandBuffer(cmd));
            VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1, .pCommandBuffers = &cmd };
            VK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
            VK(vkQueueWaitIdle(queue));

            for (unsigned lod = 0; lod < levels; lod++) {
                const unsigned w = regions[lod].imageExtent.width, h = regions[lod].imageExtent.height;
                const size_t offset = regions[lod].bufferOffset / sizeof(float);
                const float *input = mapped + offset, *output = input + total / sizeof(float);
                for (unsigned y = 0; y < h; y++) {
                    for (unsigned x = 0; x < w; x++) {
                        const bool cleared = x >= w / 4 && x < w / 4 + w / 2 &&
                                             y >= h / 4 && y < h / 4 + h / 2;
                        float expected = cleared ? (float)(2 + lod + reverse) / 16.0f : input[y * w + x];
                        if (output[y * w + x] != expected) {
                            fprintf(stderr, "%ux%u lod %u (%u,%u) reverse %u: got %.9g expected %.9g\n",
                                    cases[i].width, cases[i].height, lod, x, y, reverse, output[y * w + x], expected);
                            return 2;
                        }
                    }
                }
            }
            printf("%ux%u %u mips %s, %llu exact pixels, image allocation %llu bytes\n",
                   cases[i].width, cases[i].height, levels, reverse ? "reverse" : "forward",
                   (unsigned long long)(total / sizeof(float)), (unsigned long long)image_bytes);
        }
        vkUnmapMemory(device, staging_mem);
        vkDestroyBuffer(device, staging, NULL);
        vkFreeMemory(device, staging_mem, NULL);
        for (unsigned lod = 0; lod < levels; lod++)
            vkDestroyImageView(device, views[lod], NULL);
        vkDestroyImage(device, image, NULL);
        vkFreeMemory(device, image_mem, NULL);
    }
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
