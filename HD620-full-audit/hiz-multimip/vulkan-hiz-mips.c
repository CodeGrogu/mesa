/* Gen9 multi-mip HiZ validation for ANV on HD 620.
 *
 * Modes:
 *   sizes    Print memory requirements for in-scope and out-of-scope images.
 *            The runner compares them against INTEL_DEBUG=nohiz to show which
 *            images received a HiZ surface.
 *   correct  Exact per-pixel checks of every mip of D32 images:
 *            transfer upload -> attachment (ambiguate), partial fast/slow
 *            clears, depth-tested draws, attachment -> transfer readback,
 *            loadOp CLEAR + loadOp LOAD on one mip while the other mips are
 *            checked for preservation, and texelFetch sampling of every mip.
 *   timing   GPU timestamps for depth clears and depth-tested draw frames.
 *            Run only after "correct" passes.
 */
#define _GNU_SOURCE
#include <link.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "depth_vert.h"
#include "fetch_comp.h"

#define VK(call) do { VkResult rc = (call); if (rc != VK_SUCCESS) { \
    fprintf(stderr, "%s:%d %s returned %d\n", __FILE__, __LINE__, #call, rc); exit(1); } } while (0)
#define MAX_LEVELS 12

static VkDevice device;
static VkQueue queue;
static VkCommandPool pool;
static VkCommandBuffer cmd;
static VkPhysicalDeviceMemoryProperties types;
static VkPipeline depth_pipe, fetch_pipe;
static VkPipelineLayout depth_layout, fetch_layout;
static VkDescriptorSetLayout fetch_dsl;
static VkDescriptorPool dpool;
static VkSampler sampler;
static float timestamp_period;

static int find_driver(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size; (void)data;
    if (info->dlpi_name && strstr(info->dlpi_name, "libvulkan_intel"))
        printf("driver library: %s\n", info->dlpi_name);
    return 0;
}

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

static void begin(void)
{
    VK(vkResetCommandPool(device, pool, 0));
    VkCommandBufferBeginInfo b = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    VK(vkBeginCommandBuffer(cmd, &b));
}

static void submit(void)
{
    VkMemoryBarrier sync = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &sync, 0, NULL, 0, NULL);
    VK(vkEndCommandBuffer(cmd));
    VkSubmitInfo s = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
    VK(vkQueueSubmit(queue, 1, &s, VK_NULL_HANDLE));
    VK(vkQueueWaitIdle(queue));
}

/* Conservative full barrier between the stages this test uses. */
static void transition(VkImage image, unsigned lod, unsigned count, VkImageLayout from, VkImageLayout to)
{
    VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = from, .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, lod, count, 0, 1 } };
    if (from == VK_IMAGE_LAYOUT_UNDEFINED) b.srcAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, NULL, 0, NULL, 1, &b);
}

struct rect { unsigned x, y, w, h; };

static struct rect clip(unsigned w, unsigned h, unsigned x, unsigned y, unsigned rw, unsigned rh)
{
    if (x >= w) x = w - 1;
    if (y >= h) y = h - 1;
    if (!rw) rw = 1;
    if (!rh) rh = 1;
    if (x + rw > w) rw = w - x;
    if (y + rh > h) rh = h - y;
    return (struct rect){ x, y, rw, rh };
}

/* CPU model of the depth contents. */
static void model_fill(float *m, unsigned w, struct rect r, float v)
{
    for (unsigned y = r.y; y < r.y + r.h; y++)
        for (unsigned x = r.x; x < r.x + r.w; x++)
            m[y * w + x] = v;
}

static void model_draw_less(float *m, unsigned w, struct rect r, float z)
{
    for (unsigned y = r.y; y < r.y + r.h; y++)
        for (unsigned x = r.x; x < r.x + r.w; x++)
            if (z < m[y * w + x]) m[y * w + x] = z;
}

struct draw { struct rect scissor; float z; };

static void render(VkImageView view, unsigned w, unsigned h, VkAttachmentLoadOp load, float clear,
                   const struct rect *clear_rect, float clear_rect_value,
                   const struct draw *draws, unsigned ndraws)
{
    VkRenderingAttachmentInfo depth = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = view, .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = load, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.depthStencil = { clear, 0 } };
    VkRenderingInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .extent = { w, h } }, .layerCount = 1, .pDepthAttachment = &depth };
    vkCmdBeginRendering(cmd, &ri);
    if (clear_rect) {
        VkClearAttachment a = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .clearValue.depthStencil = { clear_rect_value, 0 } };
        VkClearRect cr = { .rect = { { clear_rect->x, clear_rect->y }, { clear_rect->w, clear_rect->h } },
            .layerCount = 1 };
        vkCmdClearAttachments(cmd, 1, &a, 1, &cr);
    }
    if (ndraws) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_pipe);
        VkViewport vp = { 0, 0, (float)w, (float)h, 0, 1 };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        for (unsigned i = 0; i < ndraws; i++) {
            VkRect2D sc = { { draws[i].scissor.x, draws[i].scissor.y },
                            { draws[i].scissor.w, draws[i].scissor.h } };
            vkCmdSetScissor(cmd, 0, 1, &sc);
            vkCmdPushConstants(cmd, depth_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float), &draws[i].z);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }
    }
    vkCmdEndRendering(cmd);
}

static VkShaderModule module(const uint32_t *code, size_t size)
{
    VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size, .pCode = code };
    VkShaderModule m;
    VK(vkCreateShaderModule(device, &ci, NULL, &m));
    return m;
}

static void create_pipelines(void)
{
    VkPushConstantRange pr = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) };
    VkPipelineLayoutCreateInfo lci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &pr };
    VK(vkCreatePipelineLayout(device, &lci, NULL, &depth_layout));
    VkShaderModule vs = module(depth_vert_spv, sizeof(depth_vert_spv));
    VkPipelineShaderStageCreateInfo stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main" };
    VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineViewportStateCreateInfo vps = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1 };
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineDepthStencilStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS };
    VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dyn };
    VkPipelineRenderingCreateInfo prc = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT };
    VkGraphicsPipelineCreateInfo gp = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &prc, .stageCount = 1, .pStages = &stage, .pVertexInputState = &vi,
        .pInputAssemblyState = &ia, .pViewportState = &vps, .pRasterizationState = &rs,
        .pMultisampleState = &ms, .pDepthStencilState = &ds, .pColorBlendState = &cb,
        .pDynamicState = &dy, .layout = depth_layout };
    VK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, NULL, &depth_pipe));
    vkDestroyShaderModule(device, vs, NULL);

    VkDescriptorSetLayoutBinding binds[2] = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL } };
    VkDescriptorSetLayoutCreateInfo dl = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2, .pBindings = binds };
    VK(vkCreateDescriptorSetLayout(device, &dl, NULL, &fetch_dsl));
    VkPushConstantRange cpr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 16 };
    VkPipelineLayoutCreateInfo cl = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &fetch_dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &cpr };
    VK(vkCreatePipelineLayout(device, &cl, NULL, &fetch_layout));
    VkShaderModule cs = module(fetch_comp_spv, sizeof(fetch_comp_spv));
    VkComputePipelineCreateInfo cp = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                   .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                   .module = cs, .pName = "main" },
        .layout = fetch_layout };
    VK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, NULL, &fetch_pipe));
    vkDestroyShaderModule(device, cs, NULL);

    VkDescriptorPoolSize ps[2] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
                                   { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 } };
    VkDescriptorPoolCreateInfo dp = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, .maxSets = 4, .poolSizeCount = 2, .pPoolSizes = ps };
    VK(vkCreateDescriptorPool(device, &dp, NULL, &dpool));
    VkSamplerCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST, .maxLod = VK_LOD_CLAMP_NONE };
    VK(vkCreateSampler(device, &sci, NULL, &sampler));
}

struct image_case {
    const char *name;
    VkFormat format;
    unsigned width, height, levels, layers;
    bool in_scope;     /* expected to receive multi-LOD HiZ */
    bool pixel_test;   /* run the D32 per-pixel test */
    bool resolve_phase; /* add INPUT_ATTACHMENT usage and run phase 3 */
};

static const struct image_case cases[] = {
    /* In scope: every LOD > 0 is 8x4 aligned. */
    { "aligned-2048x2048x4", VK_FORMAT_D32_SFLOAT, 2048, 2048, 4, 1, true, true, false },
    { "aligned-704x288x4",   VK_FORMAT_D32_SFLOAT,  704,  288, 4, 1, true, true, false },
    { "aligned-256x128x6",   VK_FORMAT_D32_SFLOAT,  256,  128, 6, 1, true, true, false },
    { "aligned-64x32x4",     VK_FORMAT_D32_SFLOAT,   64,   32, 4, 1, true, true, false },
    /* LOD0 odd, upper LODs aligned: exercises BLORP's LOD0 rectangle growth. */
    { "oddlod0-2049x1025x3", VK_FORMAT_D32_SFLOAT, 2049, 1025, 3, 1, true, true, false },
    { "oddlod0-17x9x2",      VK_FORMAT_D32_SFLOAT,   17,    9, 2, 1, true, true, false },
    /* Out of scope: some LOD > 0 is not 8x4 aligned (negative cases). */
    { "unaligned-696x288x4", VK_FORMAT_D32_SFLOAT,  696,  288, 4, 1, false, true, false },
    { "unaligned-704x280x4", VK_FORMAT_D32_SFLOAT,  704,  280, 4, 1, false, true, false },
    { "unaligned-32x32x5",   VK_FORMAT_D32_SFLOAT,   32,   32, 5, 1, false, true, false },
    { "unaligned-2049x1026x2", VK_FORMAT_D32_SFLOAT, 2049, 1026, 2, 1, false, true, false },
    { "fullchain-64x32x7",   VK_FORMAT_D32_SFLOAT,   64,   32, 7, 1, false, true, false },
    /* Out of scope by format/layers: allocation only. */
    { "d16-64x32x4",         VK_FORMAT_D16_UNORM,    64,   32, 4, 1, false, false, false },
    { "x8d24-64x32x4",       VK_FORMAT_X8_D24_UNORM_PACK32, 64, 32, 4, 1, false, false, false },
    { "d24s8-64x32x4",       VK_FORMAT_D24_UNORM_S8_UINT, 64, 32, 4, 1, false, false, false },
    { "d32s8-64x32x4",       VK_FORMAT_D32_SFLOAT_S8_UINT, 64, 32, 4, 1, false, false, false },
    { "d32-2layer-64x32x4",  VK_FORMAT_D32_SFLOAT,   64,   32, 4, 2, false, false, false },
    /* Single-LOD controls: HiZ already enabled before this change. */
    { "single-64x32x1",      VK_FORMAT_D32_SFLOAT,   64,   32, 1, 1, true, true, false },
    /* With INPUT_ATTACHMENT usage, GENERAL is AUX_INVALID on Gen9, so
     * attachment -> GENERAL is a full HiZ resolve and GENERAL -> attachment
     * an ambiguate. Phase 3 exercises both on every LOD. */
    { "resolve-704x288x4",   VK_FORMAT_D32_SFLOAT,  704,  288, 4, 1, true, true, true },
    { "resolve-256x128x6",   VK_FORMAT_D32_SFLOAT,  256,  128, 6, 1, true, true, true },
    { "resolve-17x9x2",      VK_FORMAT_D32_SFLOAT,   17,    9, 2, 1, true, true, true },
    { "resolve-unaligned-696x288x4", VK_FORMAT_D32_SFLOAT, 696, 288, 4, 1, false, true, true },
};

static VkImage make_image(const struct image_case *c, VkDeviceMemory *mem, VkDeviceSize *size)
{
    VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = c->format,
        .extent = { c->width, c->height, 1 }, .mipLevels = c->levels, .arrayLayers = c->layers,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 (c->resolve_phase ? VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT : 0) };
    VkImage image;
    VK(vkCreateImage(device, &ii, NULL, &image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, image, &req);
    *size = req.size;
    if (mem) {
        *mem = alloc(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK(vkBindImageMemory(device, image, *mem, 0));
    }
    return image;
}

static int sizes(void)
{
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        VkDeviceSize size;
        VkImage image = make_image(&cases[i], NULL, &size);
        printf("size %s %s %llu\n", cases[i].name, cases[i].in_scope ? "in-scope" : "out-of-scope",
               (unsigned long long)size);
        vkDestroyImage(device, image, NULL);
    }
    return 0;
}

static float pattern(unsigned x, unsigned y, unsigned lod, unsigned salt)
{
    return (float)(1 + (x + 7 * y + 53 * lod + 19 * salt) % 511) / 512.0f;
}

static int compare(const char *what, const struct image_case *c, unsigned lod, unsigned w, unsigned h,
                   const float *got, const float *want, unsigned long long *checked)
{
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++)
            if (memcmp(&got[y * w + x], &want[y * w + x], sizeof(float))) {
                fprintf(stderr, "FAIL %s %s lod %u (%u,%u): got %.9g expected %.9g\n",
                        c->name, what, lod, x, y, got[y * w + x], want[y * w + x]);
                return 1;
            }
    *checked += (unsigned long long)w * h;
    return 0;
}

static int pixel_test(const struct image_case *c)
{
    const unsigned levels = c->levels;
    VkDeviceMemory image_mem;
    VkDeviceSize image_bytes;
    VkImage image = make_image(c, &image_mem, &image_bytes);

    unsigned W[MAX_LEVELS], H[MAX_LEVELS];
    VkImageView views[MAX_LEVELS];
    VkBufferImageCopy regions[MAX_LEVELS];
    size_t off[MAX_LEVELS];
    size_t total = 0; /* floats */
    for (unsigned l = 0; l < levels; l++) {
        W[l] = c->width >> l ? c->width >> l : 1;
        H[l] = c->height >> l ? c->height >> l : 1;
        VkImageViewCreateInfo v = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = c->format,
            .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, l, 1, 0, 1 } };
        VK(vkCreateImageView(device, &v, NULL, &views[l]));
        off[l] = total;
        regions[l] = (VkBufferImageCopy){ .bufferOffset = total * sizeof(float),
            .imageExtent = { W[l], H[l], 1 }, .imageSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, l, 0, 1 } };
        total += (size_t)W[l] * H[l];
    }
    VkImageView all_view;
    VkImageViewCreateInfo av = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = c->format,
        .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, levels, 0, 1 } };
    VK(vkCreateImageView(device, &av, NULL, &all_view));

    /* Buffer: [upload | copy readback | sampled readback] */
    const VkDeviceSize bytes = 3 * total * sizeof(float);
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    VkBuffer buf;
    VK(vkCreateBuffer(device, &bi, NULL, &buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buf, &req);
    VkDeviceMemory buf_mem = alloc(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkBindBufferMemory(device, buf, buf_mem, 0));
    float *mapped;
    VK(vkMapMemory(device, buf_mem, 0, bytes, 0, (void **)&mapped));
    float *upload = mapped, *copied = mapped + total, *sampled = mapped + 2 * total;
    float *model = malloc(total * sizeof(float));

    VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dpool, .descriptorSetCount = 1, .pSetLayouts = &fetch_dsl };
    VkDescriptorSet set;
    VK(vkAllocateDescriptorSets(device, &dai, &set));
    VkDescriptorImageInfo dii = { sampler, all_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorBufferInfo dbi = { buf, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet wr[2] = {
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0,
          .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &dii },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1,
          .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi } };
    vkUpdateDescriptorSets(device, 2, wr, 0, NULL);

    unsigned long long checked = 0;
    int fail = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

    /* Phase 1: transfer upload -> per-mip attachment work -> readback, both orders. */
    for (unsigned reverse = 0; reverse < 2 && !fail; reverse++) {
        for (unsigned l = 0; l < levels; l++)
            for (unsigned y = 0; y < H[l]; y++)
                for (unsigned x = 0; x < W[l]; x++)
                    model[off[l] + y * W[l] + x] = upload[off[l] + y * W[l] + x] = pattern(x, y, l, reverse);
        memset(copied, 0xff, total * sizeof(float));

        begin();
        transition(image, 0, levels, layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(cmd, buf, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels, regions);
        for (unsigned o = 0; o < levels; o++) {
            const unsigned l = reverse ? levels - 1 - o : o, w = W[l], h = H[l];
            float *m = model + off[l];
            transition(image, l, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            /* Even LODs clear to the HiZ clear value (fast path); odd LODs do not. */
            const float cv = (l + reverse) % 2 == 0 ? 1.0f : (float)(2 + l + reverse) / 16.0f;
            struct rect cr = clip(w, h, w / 4, h / 4, w / 2, h / 2);
            struct draw d[2] = {
                { clip(w, h, w / 3, h / 5, w / 2, h / 2), (float)(200 + 3 * l) / 512.0f },
                { clip(w, h, 0, 0, w, h), (float)(500 + l) / 512.0f },
            };
            render(views[l], w, h, VK_ATTACHMENT_LOAD_OP_LOAD, 0, &cr, cv, d, 2);
            model_fill(m, w, cr, cv);
            model_draw_less(m, w, d[0].scissor, d[0].z);
            model_draw_less(m, w, d[1].scissor, d[1].z);
            transition(image, l, 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        }
        for (unsigned l = 0; l < levels; l++) {
            VkBufferImageCopy r = regions[l];
            r.bufferOffset += total * sizeof(float);
            vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &r);
        }
        submit();
        layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        for (unsigned l = 0; l < levels && !fail; l++)
            fail = compare(reverse ? "phase1-reverse-copy" : "phase1-forward-copy", c, l, W[l], H[l],
                           copied + off[l], model + off[l], &checked);
    }

    /* Phase 2: for each target LOD, loadOp CLEAR + draws, then loadOp LOAD +
     * draws, then sample and copy every LOD. Non-target LODs must be unchanged.
     */
    for (unsigned t = 0; t < levels && !fail; t++) {
        const unsigned w = W[t], h = H[t];
        float *m = model + off[t];
        memset(copied, 0xff, total * sizeof(float));
        memset(sampled, 0xff, total * sizeof(float));
        begin();
        transition(image, t, 1, layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        const float cv = t % 2 == 0 ? 1.0f : 0.75f;
        struct draw d1[1] = { { clip(w, h, w / 3, h / 5, w / 2, h / 2), (float)(300 + t) / 512.0f } };
        render(views[t], w, h, VK_ATTACHMENT_LOAD_OP_CLEAR, cv, NULL, 0, d1, 1);
        model_fill(m, w, clip(w, h, 0, 0, w, h), cv);
        model_draw_less(m, w, d1[0].scissor, d1[0].z);
        struct draw d2[3] = {
            /* Overlaps d1: nearer, must win. */
            { clip(w, h, w / 5, h / 3, w / 2, h / 3 + 1), (float)(290 + t) / 512.0f },
            /* Farther than everything except a 1.0 clear: HiZ must reject it
             * unless the clear value was 1.0. */
            { clip(w, h, 0, 0, w, h), (float)(450 + t) / 512.0f },
            /* Farther than everything: must never pass. */
            { clip(w, h, 0, 0, w, h), 1.0f },
        };
        render(views[t], w, h, VK_ATTACHMENT_LOAD_OP_LOAD, 0, NULL, 0, d2, 3);
        for (unsigned i = 0; i < 3; i++)
            model_draw_less(m, w, d2[i].scissor, d2[i].z);

        transition(image, t, 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (t > 0)
            transition(image, 0, t, layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (t + 1 < levels)
            transition(image, t + 1, levels - t - 1, layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fetch_pipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, fetch_layout, 0, 1, &set, 0, NULL);
        for (unsigned l = 0; l < levels; l++) {
            uint32_t pc[4] = { l, W[l], H[l], (uint32_t)(2 * total + off[l]) };
            vkCmdPushConstants(cmd, fetch_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
            vkCmdDispatch(cmd, (W[l] + 7) / 8, (H[l] + 7) / 8, 1);
        }
        transition(image, 0, levels, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        for (unsigned l = 0; l < levels; l++) {
            VkBufferImageCopy r = regions[l];
            r.bufferOffset += total * sizeof(float);
            vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &r);
        }
        submit();
        layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        /* Checker self-test: corrupt one expected interior pixel of a
         * preserved upper LOD; the run must then fail. */
        if (getenv("HIZ_TEST_PERTURB") && levels > 1 && t == 0) {
            const unsigned pl = levels - 1;
            model[off[pl] + (H[pl] / 2) * W[pl] + W[pl] / 2] += 1.0f / 1024.0f;
        }
        char what[64];
        for (unsigned l = 0; l < levels && !fail; l++) {
            snprintf(what, sizeof(what), "phase2-target%u-%s-sampled", t, l == t ? "cleared" : "preserved");
            fail = compare(what, c, l, W[l], H[l], sampled + off[l], model + off[l], &checked);
            snprintf(what, sizeof(what), "phase2-target%u-%s-copy", t, l == t ? "cleared" : "preserved");
            if (!fail)
                fail = compare(what, c, l, W[l], H[l], copied + off[l], model + off[l], &checked);
        }
    }

    /* Phase 3: per target LOD, draw, attachment -> GENERAL (full resolve),
     * copy that LOD from GENERAL, GENERAL -> attachment (ambiguate), draw
     * again, then copy every LOD. */
    for (unsigned t = 0; c->resolve_phase && t < levels && !fail; t++) {
        const unsigned w = W[t], h = H[t];
        float *m = model + off[t];
        float *mid = malloc((size_t)w * h * sizeof(float));
        memset(copied, 0xff, total * sizeof(float));
        memset(sampled, 0xff, total * sizeof(float));
        begin();
        transition(image, t, 1, layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        struct draw d1[2] = {
            { clip(w, h, w / 7, h / 3, w / 2 + 1, h / 2), (float)(100 + t) / 512.0f },
            { clip(w, h, 0, 0, w, h), (float)(480 + t) / 512.0f },
        };
        render(views[t], w, h, VK_ATTACHMENT_LOAD_OP_LOAD, 0, NULL, 0, d1, 2);
        model_draw_less(m, w, d1[0].scissor, d1[0].z);
        model_draw_less(m, w, d1[1].scissor, d1[1].z);
        memcpy(mid, m, (size_t)w * h * sizeof(float));
        transition(image, t, 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        VkBufferImageCopy r = regions[t];
        r.bufferOffset += 2 * total * sizeof(float);
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_GENERAL, buf, 1, &r);
        transition(image, t, 1, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        struct draw d2[2] = {
            { clip(w, h, w / 2, h / 6, w / 3 + 1, h / 2 + 1), (float)(90 + t) / 512.0f },
            { clip(w, h, 0, 0, w, h), 1.0f },
        };
        render(views[t], w, h, VK_ATTACHMENT_LOAD_OP_LOAD, 0, NULL, 0, d2, 2);
        model_draw_less(m, w, d2[0].scissor, d2[0].z);
        transition(image, t, 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        for (unsigned l = 0; l < levels; l++) {
            VkBufferImageCopy rr = regions[l];
            rr.bufferOffset += total * sizeof(float);
            vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &rr);
        }
        submit();
        char what[64];
        snprintf(what, sizeof(what), "phase3-target%u-resolved-general-copy", t);
        fail = compare(what, c, t, w, h, sampled + off[t], mid, &checked);
        for (unsigned l = 0; l < levels && !fail; l++) {
            snprintf(what, sizeof(what), "phase3-target%u-%s-copy", t, l == t ? "reambiguated" : "preserved");
            fail = compare(what, c, l, W[l], H[l], copied + off[l], model + off[l], &checked);
        }
        free(mid);
    }

    if (!fail)
        printf("PASS %s %s levels %u: %llu exact pixel checks, image allocation %llu bytes\n",
               c->name, c->in_scope ? "in-scope" : "out-of-scope", levels, checked,
               (unsigned long long)image_bytes);

    free(model);
    vkFreeDescriptorSets(device, dpool, 1, &set);
    vkUnmapMemory(device, buf_mem);
    vkDestroyBuffer(device, buf, NULL);
    vkFreeMemory(device, buf_mem, NULL);
    vkDestroyImageView(device, all_view, NULL);
    for (unsigned l = 0; l < levels; l++)
        vkDestroyImageView(device, views[l], NULL);
    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, image_mem, NULL);
    return fail;
}

/* GPU time of depth clears and depth-tested frames per LOD. */
static int timing(void)
{
    const struct image_case *cs[] = { &cases[0], &cases[1] };
    VkQueryPoolCreateInfo qpi = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 };
    VkQueryPool qp;
    VK(vkCreateQueryPool(device, &qpi, NULL, &qp));
    enum { reps = 64, runs = 7 };
    for (unsigned ci = 0; ci < 2; ci++) {
        const struct image_case *c = cs[ci];
        VkDeviceMemory mem;
        VkDeviceSize size;
        VkImage image = make_image(c, &mem, &size);
        VkImageView views[MAX_LEVELS];
        for (unsigned l = 0; l < c->levels; l++) {
            VkImageViewCreateInfo v = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = c->format,
                .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, l, 1, 0, 1 } };
            VK(vkCreateImageView(device, &v, NULL, &views[l]));
        }
        begin();
        transition(image, 0, c->levels, VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        submit();
        for (unsigned l = 0; l < c->levels; l++) {
            const unsigned w = c->width >> l, h = c->height >> l;
            for (unsigned kind = 0; kind < 2; kind++) {
                double best = 1e30, sum = 0;
                for (unsigned run = 0; run < runs; run++) {
                    begin();
                    vkCmdResetQueryPool(cmd, qp, 0, 2);
                    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 0);
                    for (unsigned r = 0; r < reps; r++) {
                        if (kind == 0) {
                            render(views[l], w, h, VK_ATTACHMENT_LOAD_OP_CLEAR, 1.0f, NULL, 0, NULL, 0);
                        } else {
                            /* A "frame": clear, then 8 full-screen layers back to front
                             * and 8 front to back, so HiZ can reject the occluded half. */
                            struct draw d[16];
                            for (unsigned i = 0; i < 8; i++) {
                                d[i] = (struct draw){ { 0, 0, w, h }, (float)(400 - 10 * i) / 512.0f };
                                d[8 + i] = (struct draw){ { 0, 0, w, h }, (float)(450 + 5 * i) / 512.0f };
                            }
                            render(views[l], w, h, VK_ATTACHMENT_LOAD_OP_CLEAR, 1.0f, NULL, 0, d, 16);
                        }
                        VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT };
                        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
                    }
                    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 1);
                    submit();
                    uint64_t ts[2];
                    VK(vkGetQueryPoolResults(device, qp, 0, 2, sizeof(ts), ts, sizeof(uint64_t),
                                             VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
                    double us = (double)(ts[1] - ts[0]) * timestamp_period / 1000.0 / reps;
                    if (run > 0) { sum += us; if (us < best) best = us; } /* run 0 is warm-up */
                }
                printf("timing %s lod %u %ux%u %s: best %.2f us mean %.2f us per op (%d reps x %d runs)\n",
                       c->name, l, w, h, kind ? "clear+16-draw frame" : "loadOp-clear", best,
                       sum / (runs - 1), reps, runs - 1);
            }
        }
        for (unsigned l = 0; l < c->levels; l++)
            vkDestroyImageView(device, views[l], NULL);
        vkDestroyImage(device, image, NULL);
        vkFreeMemory(device, mem, NULL);
    }
    vkDestroyQueryPool(device, qp, NULL);
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "correct";
    setvbuf(stdout, NULL, _IOLBF, 0);
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ic = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance instance;
    VK(vkCreateInstance(&ic, NULL, &instance));
    uint32_t count = 1;
    VkPhysicalDevice gpu;
    VkResult er = vkEnumeratePhysicalDevices(instance, &count, &gpu);
    if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || count < 1) return 1;
    VkPhysicalDeviceDriverProperties drv = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
    VkPhysicalDeviceProperties2 props = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &drv };
    vkGetPhysicalDeviceProperties2(gpu, &props);
    if (props.properties.vendorID != 0x8086 || props.properties.deviceID != 0x5916) return 1;
    timestamp_period = props.properties.limits.timestampPeriod;
    vkGetPhysicalDeviceMemoryProperties(gpu, &types);
    printf("%s driverVersion %u driverName '%s' driverInfo '%s' mode %s\n", props.properties.deviceName,
           props.properties.driverVersion, drv.driverName, drv.driverInfo, mode);

    const float priority = 1;
    VkDeviceQueueCreateInfo qc = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueCount = 1, .pQueuePriorities = &priority };
    VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE };
    VkDeviceCreateInfo dc = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f13,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qc };
    VK(vkCreateDevice(gpu, &dc, NULL, &device));
    dl_iterate_phdr(find_driver, NULL);
    vkGetDeviceQueue(device, 0, 0, &queue);
    VkCommandPoolCreateInfo cp = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VK(vkCreateCommandPool(device, &cp, NULL, &pool));
    VkCommandBufferAllocateInfo ca = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .commandBufferCount = 1 };
    VK(vkAllocateCommandBuffers(device, &ca, &cmd));
    create_pipelines();

    int fail = 0;
    if (!strcmp(mode, "sizes")) {
        fail = sizes();
    } else if (!strcmp(mode, "timing")) {
        fail = timing();
    } else {
        const char *only = argc > 2 ? argv[2] : NULL;
        unsigned passed = 0, run = 0;
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            if (!cases[i].pixel_test || (only && strcmp(only, cases[i].name))) continue;
            run++;
            if (pixel_test(&cases[i])) fail = 2; else passed++;
        }
        printf("SUMMARY %u/%u pixel cases passed\n", passed, run);
    }
    vkDeviceWaitIdle(device);
    vkDestroyPipeline(device, depth_pipe, NULL);
    vkDestroyPipeline(device, fetch_pipe, NULL);
    vkDestroyPipelineLayout(device, depth_layout, NULL);
    vkDestroyPipelineLayout(device, fetch_layout, NULL);
    vkDestroyDescriptorPool(device, dpool, NULL);
    vkDestroyDescriptorSetLayout(device, fetch_dsl, NULL);
    vkDestroySampler(device, sampler, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return fail;
}
