/* Gfx9 ASTC 5x5 + CCS sampler hang check (Mesa #1892).
 *
 * Renders a gradient into an R8G8B8A8 colour image that is also sampled
 * (CCS_E on Gfx9), uploads random ASTC blocks into a second image, then
 * dispatches a compute shader that samples both in one loop.
 *
 * usage: astc-ccs fullscreen.vert.spv gradient.frag.spv mix-sample.comp.spv FORMAT
 *   FORMAT: 5x5 or 4x4 (control: Gfx9 has no 4x4 restriction)
 *   REPEAT=n  submissions (default 50), each with 8 dispatches
 * Exit 0 when every submission completes, 4 on VK_ERROR_DEVICE_LOST or a
 * 30 s timeout, 2 on setup failure.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define VK(call) do { VkResult rc_ = (call); if (rc_ != VK_SUCCESS) { \
    fprintf(stderr, "%s:%d %s returned %d\n", __FILE__, __LINE__, #call, rc_); \
    exit(rc_ == VK_ERROR_DEVICE_LOST ? 4 : 2); } } while (0)

#ifndef TEX
#define TEX 256
#endif
#ifndef GRID
#define GRID 256
#endif
enum { W = TEX, H = TEX };
static VkDevice dev;
static VkPhysicalDeviceMemoryProperties mp;

static VkDeviceMemory alloc_bind(VkMemoryRequirements req, VkMemoryPropertyFlags want)
{
    uint32_t t = 0;
    while (t < mp.memoryTypeCount && !((req.memoryTypeBits & (1u << t)) && (mp.memoryTypes[t].propertyFlags & want) == want))
        t++;
    if (t == mp.memoryTypeCount) { fprintf(stderr, "no memory type\n"); exit(2); }
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = t };
    VkDeviceMemory m;
    VK(vkAllocateMemory(dev, &ai, NULL, &m));
    return m;
}

static VkShaderModule module(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    static uint32_t code[1 << 14];
    size_t n = fread(code, 1, sizeof(code), f);
    fclose(f);
    VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = n, .pCode = code };
    VkShaderModule m;
    VK(vkCreateShaderModule(dev, &ci, NULL, &m));
    return m;
}

static void barrier(VkCommandBuffer c, VkImage img, VkImageLayout from, VkImageLayout to,
                    VkPipelineStageFlags2 ss, VkAccessFlags2 sa, VkPipelineStageFlags2 ds, VkAccessFlags2 da)
{
    VkImageMemoryBarrier2 b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = ss, .srcAccessMask = sa, .dstStageMask = ds, .dstAccessMask = da,
        .oldLayout = from, .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VkDependencyInfo d = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b };
    vkCmdPipelineBarrier2(c, &d);
}

static VkImage make_image(VkFormat fmt, VkImageUsageFlags usage, VkImageView *view)
{
    VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = fmt, .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage };
    VkImage img;
    VK(vkCreateImage(dev, &ici, NULL, &img));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, img, &req);
    VK(vkBindImageMemory(dev, img, alloc_bind(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), 0));
    VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VK(vkCreateImageView(dev, &vci, NULL, view));
    return img;
}

int main(int argc, char **argv)
{
    if (argc != 5) return 2;
    const int is5 = !strcmp(argv[4], "5x5");
    const VkFormat astc_fmt = is5 ? VK_FORMAT_ASTC_5x5_UNORM_BLOCK : VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
    const int bw = is5 ? 5 : 4;
    const int repeat = getenv("REPEAT") ? atoi(getenv("REPEAT")) : 50;

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    VK(vkCreateInstance(&ii, NULL, &inst));
    uint32_t n = 1;
    VkPhysicalDevice phys;
    VkResult er = vkEnumeratePhysicalDevices(inst, &n, &phys);
    if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || !n) return 2;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE, .synchronization2 = VK_TRUE };
    VkPhysicalDeviceFeatures2 f2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f13,
        .features = { .textureCompressionASTC_LDR = VK_TRUE } };
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f2,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi };
    VK(vkCreateDevice(phys, &di, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    VkImageView ccs_view, astc_view;
    VkImage ccs = make_image(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &ccs_view);
    VkImage astc = make_image(astc_fmt, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, &astc_view);

    /* Staging with random ASTC blocks, output buffer */
    const int nb = ((W + bw - 1) / bw) * ((H + bw - 1) / bw);
    VkBufferCreateInfo sbi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = nb * 16,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT };
    VkBuffer stage;
    VK(vkCreateBuffer(dev, &sbi, NULL, &stage));
    VkMemoryRequirements sreq;
    vkGetBufferMemoryRequirements(dev, stage, &sreq);
    VkDeviceMemory smem = alloc_bind(sreq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkBindBufferMemory(dev, stage, smem, 0));
    uint8_t *sm;
    VK(vkMapMemory(dev, smem, 0, VK_WHOLE_SIZE, 0, (void **)&sm));
    uint32_t rng = 0xdeadbeefu;
    for (int i = 0; i < nb * 16; i++) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; sm[i] = (uint8_t)rng; }
    /* ASTC_FILE: real encoded blocks from an .astc file (16-byte header). */
    if (getenv("ASTC_FILE")) {
        FILE *af = fopen(getenv("ASTC_FILE"), "rb");
        if (!af || fseek(af, 16, SEEK_SET) || fread(sm, 16, nb, af) != (size_t)nb) {
            fprintf(stderr, "%s: need %d blocks\n", getenv("ASTC_FILE"), nb);
            return 2;
        }
        fclose(af);
    }
    VkBufferCreateInfo obi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = W * H * 16,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    VkBuffer out;
    VK(vkCreateBuffer(dev, &obi, NULL, &out));
    VkMemoryRequirements oreq;
    vkGetBufferMemoryRequirements(dev, out, &oreq);
    VK(vkBindBufferMemory(dev, out, alloc_bind(oreq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), 0));

    /* Gradient pipeline */
    VkPipelineLayoutCreateInfo gli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkPipelineLayout gl;
    VK(vkCreatePipelineLayout(dev, &gli, NULL, &gl));
    VkPipelineShaderStageCreateInfo st[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = module(argv[1]), .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = module(argv[2]), .pName = "main" } };
    VkPipelineVertexInputStateCreateInfo vis = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ias = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkViewport vp = { 0, 0, W, H, 0, 1 };
    VkRect2D sc = { { 0, 0 }, { W, H } };
    VkPipelineViewportStateCreateInfo vps = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo msi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xf };
    VkPipelineColorBlendStateCreateInfo cbs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cba };
    VkFormat cf = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo pri = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1, .pColorAttachmentFormats = &cf };
    VkGraphicsPipelineCreateInfo gpi = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &pri,
        .stageCount = 2, .pStages = st, .pVertexInputState = &vis, .pInputAssemblyState = &ias,
        .pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &msi, .pColorBlendState = &cbs, .layout = gl };
    VkPipeline gp;
    VK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpi, NULL, &gp));

    /* Sampling pipeline */
    VkDescriptorSetLayoutBinding lb[3] = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL } };
    VkDescriptorSetLayoutCreateInfo li = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3, .pBindings = lb };
    VkDescriptorSetLayout dsl;
    VK(vkCreateDescriptorSetLayout(dev, &li, NULL, &dsl));
    VkPipelineLayoutCreateInfo cli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl };
    VkPipelineLayout cl;
    VK(vkCreatePipelineLayout(dev, &cli, NULL, &cl));
    VkComputePipelineCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                   .module = module(argv[3]), .pName = "main" }, .layout = cl };
    VkPipeline cp;
    VK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpi, NULL, &cp));
    VkSamplerCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR, .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT, .maxLod = 0 };
    VkSampler smp;
    VK(vkCreateSampler(dev, &sci, NULL, &smp));
    VkDescriptorPoolSize ps[2] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 } };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = ps };
    VkDescriptorPool dp;
    VK(vkCreateDescriptorPool(dev, &dpi, NULL, &dp));
    VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dp, .descriptorSetCount = 1, .pSetLayouts = &dsl };
    VkDescriptorSet set;
    VK(vkAllocateDescriptorSets(dev, &dai, &set));
    VkDescriptorImageInfo i0 = { smp, ccs_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo i1 = { smp, astc_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorBufferInfo b2 = { out, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet w[3] = {
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0, .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &i0 },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1, .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &i1 },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 2, .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &b2 } };
    vkUpdateDescriptorSets(dev, 3, w, 0, NULL);

    VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandPool pool;
    VK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cb;
    VK(vkAllocateCommandBuffers(dev, &cai, &cb));
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    VK(vkCreateFence(dev, &fci, NULL, &fence));

    for (int r = 0; r < repeat; r++) {
        VK(vkResetCommandPool(dev, pool, 0));
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VK(vkBeginCommandBuffer(cb, &bi));
        /* Fresh compressed render and ASTC upload each submission. */
        barrier(cb, ccs, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo att = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = ccs_view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
        VkRenderingInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = { { 0, 0 }, { W, H } },
            .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &att };
        vkCmdBeginRendering(cb, &ri);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gp);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRendering(cb);
        barrier(cb, ccs, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        barrier(cb, astc, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy rg = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { W, H, 1 } };
        vkCmdCopyBufferToImage(cb, stage, astc, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        barrier(cb, astc, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cp);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cl, 0, 1, &set, 0, NULL);
        for (int d = 0; d < 8; d++)
            vkCmdDispatch(cb, GRID / 8, GRID / 8, 1);
        VK(vkEndCommandBuffer(cb));
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
        VK(vkResetFences(dev, 1, &fence));
        VK(vkQueueSubmit(queue, 1, &si, fence));
        VkResult wr = vkWaitForFences(dev, 1, &fence, VK_TRUE, 30000000000ull);
        if (wr != VK_SUCCESS) {
            printf("ASTC %s submission %d: %s (%d)\n", argv[4], r,
                   wr == VK_ERROR_DEVICE_LOST ? "VK_ERROR_DEVICE_LOST" : wr == VK_TIMEOUT ? "VK_TIMEOUT" : "error", wr);
            return 4;
        }
    }
    printf("ASTC %s: %d submissions completed\n", argv[4], repeat);
    return 0;
}
