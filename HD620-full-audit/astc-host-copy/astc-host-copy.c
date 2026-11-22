/* Compare ASTC uploads through VK_EXT_host_image_copy against
 * vkCmdCopyBufferToImage on the same data.
 *
 * Every 4x4 block is an LDR void-extent (constant colour) block whose RGBA
 * values are multiples of 257, so each texel has an exact expected value.
 *
 * usage: astc-host-copy sample.spv
 * Exit 0: every texel of every case matched. Exit 1: mismatch. Exit 2: setup.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define VK(call) do { VkResult rc_ = (call); if (rc_ != VK_SUCCESS) { \
    fprintf(stderr, "%s:%d %s returned %d\n", __FILE__, __LINE__, #call, rc_); exit(2); } } while (0)

enum { W = 64, H = 64, BW = W / 4, BH = H / 4, NBLOCK = BW * BH };

static VkDevice dev;
static VkPhysicalDevice phys;
static VkQueue queue;
static VkCommandPool cpool;
static VkPhysicalDeviceMemoryProperties mprops;
static PFN_vkCopyMemoryToImageEXT copy_memory_to_image;
static PFN_vkTransitionImageLayoutEXT transition_layout;
static PFN_vkCopyImageToImageEXT copy_image_to_image;

static uint32_t find_type(uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t t = 0; t < mprops.memoryTypeCount; t++)
        if ((bits & (1u << t)) && (mprops.memoryTypes[t].propertyFlags & want) == want)
            return t;
    fprintf(stderr, "no memory type for %#x\n", want);
    exit(2);
}

static VkDeviceMemory alloc_for(VkMemoryRequirements req, VkMemoryPropertyFlags want)
{
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = find_type(req.memoryTypeBits, want) };
    VkDeviceMemory m;
    VK(vkAllocateMemory(dev, &ai, NULL, &m));
    return m;
}

static VkBuffer make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkDeviceMemory *mem, void **map)
{
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage };
    VkBuffer b;
    VK(vkCreateBuffer(dev, &bi, NULL, &b));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, b, &req);
    *mem = alloc_for(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkBindBufferMemory(dev, b, *mem, 0));
    VK(vkMapMemory(dev, *mem, 0, VK_WHOLE_SIZE, 0, map));
    return b;
}

static VkCommandBuffer begin(void)
{
    VkCommandBufferAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cpool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer c;
    VK(vkAllocateCommandBuffers(dev, &ai, &c));
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    VK(vkBeginCommandBuffer(c, &bi));
    return c;
}

static void submit(VkCommandBuffer c)
{
    VK(vkEndCommandBuffer(c));
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &c };
    VK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    VK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(dev, cpool, 1, &c);
}

static void barrier(VkCommandBuffer c, VkImage img, VkImageLayout from, VkImageLayout to,
                    VkPipelineStageFlags src_stage, VkAccessFlags src, VkPipelineStageFlags dst_stage, VkAccessFlags dst)
{
    VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src, .dstAccessMask = dst, .oldLayout = from, .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    vkCmdPipelineBarrier(c, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
}

/* LDR void-extent block: bits 0..8 = 0x1fc, bit 9 = 0 (LDR), bits 10..11 = 1,
 * bits 12..63 all ones (no extent coordinates), then RGBA as UNORM16. */
static void void_extent(uint8_t out[16], const uint16_t rgba[4])
{
    uint64_t lo = 0x1fcull | (3ull << 10) | (0xfffffffffffffull << 12);
    uint64_t hi = (uint64_t)rgba[0] | ((uint64_t)rgba[1] << 16) |
                  ((uint64_t)rgba[2] << 32) | ((uint64_t)rgba[3] << 48);
    memcpy(out, &lo, 8);
    memcpy(out + 8, &hi, 8);
}

static float srgb_to_linear(float c)
{
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: %s sample.spv\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static uint32_t code[1 << 14];
    size_t bytes = fread(code, 1, sizeof(code), f);
    fclose(f);
    if (bytes < 20 || bytes % 4 || bytes == sizeof(code)) return 2;

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    VK(vkCreateInstance(&ii, NULL, &inst));
    uint32_t n = 1;
    VkResult er = vkEnumeratePhysicalDevices(inst, &n, &phys);
    if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || !n) return 2;

    VkPhysicalDeviceVulkan12Properties p12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
    VkPhysicalDeviceProperties2 props = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &p12 };
    vkGetPhysicalDeviceProperties2(phys, &props);
    printf("device %s, driver %s\n", props.properties.deviceName, p12.driverInfo);
    vkGetPhysicalDeviceMemoryProperties(phys, &mprops);

    VkPhysicalDeviceHostImageCopyFeaturesEXT hic = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT };
    VkPhysicalDeviceFeatures2 feats = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &hic };
    vkGetPhysicalDeviceFeatures2(phys, &feats);
    if (!hic.hostImageCopy || !feats.features.textureCompressionASTC_LDR) {
        fprintf(stderr, "hostImageCopy=%u ASTC_LDR=%u\n", hic.hostImageCopy, feats.features.textureCompressionASTC_LDR);
        return 2;
    }

    VkPhysicalDeviceHostImageCopyFeaturesEXT ehic = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT,
        .hostImageCopy = VK_TRUE };
    VkPhysicalDeviceFeatures2 ef = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &ehic,
        .features = { .textureCompressionASTC_LDR = VK_TRUE } };
    const char *exts[] = { VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME };
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &ef,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi, .enabledExtensionCount = 1, .ppEnabledExtensionNames = exts };
    VK(vkCreateDevice(phys, &di, NULL, &dev));
    vkGetDeviceQueue(dev, 0, 0, &queue);
    copy_memory_to_image = (PFN_vkCopyMemoryToImageEXT)vkGetDeviceProcAddr(dev, "vkCopyMemoryToImageEXT");
    transition_layout = (PFN_vkTransitionImageLayoutEXT)vkGetDeviceProcAddr(dev, "vkTransitionImageLayoutEXT");
    copy_image_to_image = (PFN_vkCopyImageToImageEXT)vkGetDeviceProcAddr(dev, "vkCopyImageToImageEXT");
    if (!copy_memory_to_image || !transition_layout || !copy_image_to_image) return 2;

    VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 };
    VK(vkCreateCommandPool(dev, &cpi, NULL, &cpool));

    /* Pipeline */
    VkDescriptorSetLayoutBinding lb[2] = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL } };
    VkDescriptorSetLayoutCreateInfo li = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2, .pBindings = lb };
    VkDescriptorSetLayout dsl;
    VK(vkCreateDescriptorSetLayout(dev, &li, NULL, &dsl));
    VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 };
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr };
    VkPipelineLayout pl;
    VK(vkCreatePipelineLayout(dev, &pli, NULL, &pl));
    VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = bytes, .pCode = code };
    VkShaderModule sm;
    VK(vkCreateShaderModule(dev, &smi, NULL, &sm));
    VkComputePipelineCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                   .module = sm, .pName = "main" }, .layout = pl };
    VkPipeline pipe;
    VK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pci, NULL, &pipe));
    VkSamplerCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST, .maxLod = 0 };
    VkSampler sampler;
    VK(vkCreateSampler(dev, &sci, NULL, &sampler));
    VkDescriptorPoolSize ps[2] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16 } };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 16, .poolSizeCount = 2, .pPoolSizes = ps };
    VkDescriptorPool dpool;
    VK(vkCreateDescriptorPool(dev, &dpi, NULL, &dpool));

    /* Block data */
    static uint8_t blocks[NBLOCK * 16];
    static uint16_t colors[NBLOCK][4];
    uint32_t rng = 0x12345678u;
    for (int b = 0; b < NBLOCK; b++) {
        for (int c = 0; c < 4; c++) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            colors[b][c] = (uint16_t)((rng & 0xff) * 257);
        }
        /* Every 7th block carries flushed-denorm-range components (1..3). */
        if (b % 7 == 3) { colors[b][0] = 1; colors[b][2] = 3; }
        void_extent(blocks + b * 16, colors[b]);
    }
    /* ASTC_DUMP=dir: fill with random (mostly non-void-extent) blocks, skip
     * the exact check and write each sampled image to dir for comparison
     * between drivers. */
    const char *dump = getenv("ASTC_DUMP");
    if (dump) {
        for (unsigned i = 0; i < sizeof(blocks); i++) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            blocks[i] = (uint8_t)rng;
        }
    }
    VkDeviceMemory stage_mem, out_mem;
    void *stage_map, *out_map;
    VkBuffer stage = make_buffer(sizeof(blocks), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &stage_mem, &stage_map);
    memcpy(stage_map, blocks, sizeof(blocks));
    VkBuffer out = make_buffer(W * H * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &out_mem, &out_map);

    const struct { VkFormat fmt; const char *name; int srgb; } formats[] = {
        { VK_FORMAT_ASTC_4x4_UNORM_BLOCK, "ASTC_4x4_UNORM", 0 },
        { VK_FORMAT_ASTC_4x4_SRGB_BLOCK, "ASTC_4x4_SRGB", 1 },
        { VK_FORMAT_ASTC_8x8_UNORM_BLOCK, "ASTC_8x8_UNORM", 0 },
        { VK_FORMAT_ASTC_5x5_UNORM_BLOCK, "ASTC_5x5_UNORM", 0 },
        { VK_FORMAT_ASTC_5x5_SRGB_BLOCK, "ASTC_5x5_SRGB", 1 },
    };
    unsigned failures = 0;

    for (unsigned fi = 0; fi < sizeof(formats) / sizeof(formats[0]); fi++) {
        const VkFormat fmt = formats[fi].fmt;
        const int bw = fmt == VK_FORMAT_ASTC_8x8_UNORM_BLOCK ? 8 :
                       (fmt == VK_FORMAT_ASTC_5x5_UNORM_BLOCK || fmt == VK_FORMAT_ASTC_5x5_SRGB_BLOCK) ? 5 : 4;
        const int nbx = (W + bw - 1) / bw, nby = (H + bw - 1) / bw;
        for (int path = 0; path < 3; path++) {
            const char *pname = path == 2 ? "host-i2i" : path ? "host-copy" : "cmd-copy";
            VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                .format = formats[fi].fmt, .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_SAMPLED_BIT | (path ? VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT
                                                             : VK_IMAGE_USAGE_TRANSFER_DST_BIT),
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
            VkImage img;
            VK(vkCreateImage(dev, &ici, NULL, &img));
            VkMemoryRequirements req;
            vkGetImageMemoryRequirements(dev, img, &req);
            VkDeviceMemory imem = alloc_for(req, path ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT : 0);
            VK(vkBindImageMemory(dev, img, imem, 0));

            VkImage src_img = VK_NULL_HANDLE;
            VkDeviceMemory src_mem = VK_NULL_HANDLE;
            if (path == 2) {
                VK(vkCreateImage(dev, &ici, NULL, &src_img));
                VkMemoryRequirements sreq;
                vkGetImageMemoryRequirements(dev, src_img, &sreq);
                src_mem = alloc_for(sreq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
                VK(vkBindImageMemory(dev, src_img, src_mem, 0));
            }
            if (path) {
                VkImage upload = path == 2 ? src_img : img;
                VkHostImageLayoutTransitionInfoEXT t[2] = {
                    { .sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT,
                      .image = img, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                      .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } },
                    { .sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT,
                      .image = src_img, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                      .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } } };
                VK(transition_layout(dev, path == 2 ? 2 : 1, t));
                VkMemoryToImageCopyEXT r = { .sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT,
                    .pHostPointer = blocks, .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                    .imageExtent = { W, H, 1 } };
                VkCopyMemoryToImageInfoEXT ci = { .sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT,
                    .dstImage = upload, .dstImageLayout = VK_IMAGE_LAYOUT_GENERAL, .regionCount = 1, .pRegions = &r };
                VK(copy_memory_to_image(dev, &ci));
                if (path == 2) {
                    VkImageCopy2 ir = { .sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2,
                        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .extent = { W, H, 1 } };
                    VkCopyImageToImageInfoEXT ii2 = { .sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_IMAGE_INFO_EXT,
                        .srcImage = src_img, .srcImageLayout = VK_IMAGE_LAYOUT_GENERAL,
                        .dstImage = img, .dstImageLayout = VK_IMAGE_LAYOUT_GENERAL, .regionCount = 1, .pRegions = &ir };
                    VK(copy_image_to_image(dev, &ii2));
                }
            } else {
                VkCommandBuffer c = begin();
                barrier(c, img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                VkBufferImageCopy r = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { W, H, 1 } };
                vkCmdCopyBufferToImage(c, stage, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
                barrier(c, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
                submit(c);
            }

            VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
                .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = formats[fi].fmt,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
            VkImageView view;
            VK(vkCreateImageView(dev, &vci, NULL, &view));
            VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = dpool, .descriptorSetCount = 1, .pSetLayouts = &dsl };
            VkDescriptorSet set;
            VK(vkAllocateDescriptorSets(dev, &dai, &set));
            VkDescriptorImageInfo dii = { sampler, view, VK_IMAGE_LAYOUT_GENERAL };
            VkDescriptorBufferInfo dbi = { out, 0, VK_WHOLE_SIZE };
            VkWriteDescriptorSet w[2] = {
                { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0, .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &dii },
                { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1, .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi } };
            vkUpdateDescriptorSets(dev, 2, w, 0, NULL);

            memset(out_map, 0xcc, W * H * 16);
            VkCommandBuffer c = begin();
            vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
            vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
            uint32_t width = W;
            vkCmdPushConstants(c, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &width);
            vkCmdDispatch(c, W / 8, H / 8, 1);
            VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
            vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
            submit(c);

            const float *o = out_map;
            if (dump) {
                char path_buf[512];
                snprintf(path_buf, sizeof(path_buf), "%s/%s-%s.bin", dump, formats[fi].name, pname);
                FILE *df = fopen(path_buf, "wb");
                if (!df || fwrite(o, 16, W * H, df) != W * H) { perror(path_buf); return 2; }
                fclose(df);
                printf("%-15s %-9s dumped\n", formats[fi].name, pname);
                vkDestroyImageView(dev, view, NULL);
                vkDestroyImage(dev, img, NULL);
                vkFreeMemory(dev, imem, NULL);
                if (src_img) { vkDestroyImage(dev, src_img, NULL); vkFreeMemory(dev, src_mem, NULL); }
                continue;
            }
            unsigned bad = 0;
            float worst = 0;
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    /* Each format reads the first nbx*nby blocks of the same
                     * tightly packed buffer. */
                    int b = (y / bw) * nbx + x / bw;
                    (void)nby;
                    for (int ch = 0; ch < 4; ch++) {
                        float want = colors[b][ch] / 65535.0f;
                        if (formats[fi].srgb && ch < 3) want = srgb_to_linear(want);
                        float got = o[(y * W + x) * 4 + ch];
                        float d = fabsf(got - want);
                        if (!(d <= 1.5f / 255.0f)) {
                            if (bad < 3)
                                printf("    (%d,%d) ch%d got %f want %f\n", x, y, ch, got, want);
                            bad++;
                        }
                        if (d > worst || d != d) worst = d;
                    }
                }
            }
            printf("%-15s %-9s %6u/%u channel mismatches, worst error %g\n",
                   formats[fi].name, pname, bad, W * H * 4, worst);
            failures += bad;
            vkDestroyImageView(dev, view, NULL);
            vkDestroyImage(dev, img, NULL);
            vkFreeMemory(dev, imem, NULL);
            if (src_img) { vkDestroyImage(dev, src_img, NULL); vkFreeMemory(dev, src_mem, NULL); }
        }
    }
    printf("%s\n", failures ? "FAIL" : "PASS");
    vkDestroyDescriptorPool(dev, dpool, NULL);
    vkDestroySampler(dev, sampler, NULL);
    vkDestroyPipeline(dev, pipe, NULL);
    vkDestroyShaderModule(dev, sm, NULL);
    vkDestroyPipelineLayout(dev, pl, NULL);
    vkDestroyDescriptorSetLayout(dev, dsl, NULL);
    vkDestroyBuffer(dev, stage, NULL);
    vkDestroyBuffer(dev, out, NULL);
    vkFreeMemory(dev, stage_mem, NULL);
    vkFreeMemory(dev, out_mem, NULL);
    vkDestroyCommandPool(dev, cpool, NULL);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);
    return failures ? 1 : 0;
}
