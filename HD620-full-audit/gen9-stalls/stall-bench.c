/* Gen9 flush/stall micro-benchmark.
 *
 * Each scenario records ITER iterations into one command buffer, brackets
 * them with timestamps, and reports the median GPU and CPU-recording time
 * per iteration over REPS submissions.  The colour image is read back
 * afterwards: the triangle area must be red and the rest the clear colour.
 *
 *   vf     transfer write to the vertex buffer, barrier to vertex input,
 *          then a draw in its own rendering scope (VF cache invalidate)
 *   push   one rendering scope; alternate two pipelines, push, draw
 *          (push constant flush per draw)
 *   vfe    dispatch, then a draw in its own rendering scope
 *          (PIPELINE_SELECT GPGPU -> 3D)
 *   load   LOAD_OP_LOAD rendering scope per draw on a fast-cleared image
 *          (clear colour reload into the surface state)
 *   blend  correctness: per iteration, fast clear to one of three colours,
 *          then additively blend the red triangle over it in a LOAD scope
 *          and copy pixel (1,1) out.  Blending reads the clear colour from
 *          the surface state, so a stale state cache shows up as a wrong
 *          colour in that iteration's slot.
 *
 * usage: stall-bench SCENARIO tri.vert.spv tri.frag.spv tick.comp.spv
 * Output: one line "scenario gpu_ns_per_iter cpu_ns_per_iter pixels=ok|BAD".
 * Exit 1 if the read-back check fails, 2 on setup failure.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#define VK(call) do { VkResult rc_ = (call); if (rc_ != VK_SUCCESS) { \
    fprintf(stderr, "%s:%d %s returned %d\n", __FILE__, __LINE__, #call, rc_); exit(2); } } while (0)

enum { W = 256, H = 256, ITER = 1000, REPS = 9, BLEND_ITER = 300 };

static VkDevice dev;
static VkPhysicalDeviceMemoryProperties mprops;

static uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000ull + t.tv_nsec;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static VkShaderModule load_module(const char *path)
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

static VkDeviceMemory bind_memory(VkMemoryRequirements req, VkMemoryPropertyFlags want)
{
    for (uint32_t t = 0; t < mprops.memoryTypeCount; t++) {
        if ((req.memoryTypeBits & (1u << t)) && (mprops.memoryTypes[t].propertyFlags & want) == want) {
            VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = req.size, .memoryTypeIndex = t };
            VkDeviceMemory m;
            VK(vkAllocateMemory(dev, &ai, NULL, &m));
            return m;
        }
    }
    fprintf(stderr, "no memory type\n");
    exit(2);
}

static VkBuffer make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, void **map)
{
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage };
    VkBuffer b;
    VK(vkCreateBuffer(dev, &bi, NULL, &b));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, b, &req);
    VkDeviceMemory m = bind_memory(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkBindBufferMemory(dev, b, m, 0));
    VK(vkMapMemory(dev, m, 0, VK_WHOLE_SIZE, 0, map));
    return b;
}

static void image_barrier(VkCommandBuffer c, VkImage img, VkImageLayout from, VkImageLayout to,
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

static void memory_barrier(VkCommandBuffer c, VkPipelineStageFlags2 ss, VkAccessFlags2 sa,
                           VkPipelineStageFlags2 ds, VkAccessFlags2 da)
{
    VkMemoryBarrier2 b = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = ss, .srcAccessMask = sa, .dstStageMask = ds, .dstAccessMask = da };
    VkDependencyInfo d = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &b };
    vkCmdPipelineBarrier2(c, &d);
}

int main(int argc, char **argv)
{
    if (argc != 5) { fprintf(stderr, "usage: %s vf|push|vfe|load vert frag comp\n", argv[0]); return 2; }
    const char *sc = argv[1];
    const int vf = !strcmp(sc, "vf"), push = !strcmp(sc, "push"), vfe = !strcmp(sc, "vfe"), load = !strcmp(sc, "load");
    const int blend = !strcmp(sc, "blend");
    /* blend2: one fast clear per submission, colour changing between
     * submissions (the state memory is reused after the pool reset), then
     * ITER LOAD scopes without barriers, each blending a 6-pixel triangle
     * onto its own untouched 8x8 cell, so every scope reads the clear colour
     * through a freshly loaded surface state. */
    const int blend2 = !strcmp(sc, "blend2");
    if (!(vf || push || vfe || load || blend || blend2)) return 2;

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    VK(vkCreateInstance(&ii, NULL, &inst));
    uint32_t n = 1;
    VkPhysicalDevice phys;
    VkResult er = vkEnumeratePhysicalDevices(inst, &n, &phys);
    if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || !n) return 2;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(phys, &props);
    vkGetPhysicalDeviceMemoryProperties(phys, &mprops);

    VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE, .synchronization2 = VK_TRUE };
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f13,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi };
    VK(vkCreateDevice(phys, &di, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    /* Colour target (optimal, compressible) */
    VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
    VkImage img;
    VK(vkCreateImage(dev, &ici, NULL, &img));
    VkMemoryRequirements ireq;
    vkGetImageMemoryRequirements(dev, img, &ireq);
    VK(vkBindImageMemory(dev, img, bind_memory(ireq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), 0));
    VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VkImageView view;
    VK(vkCreateImageView(dev, &vci, NULL, &view));

    void *vb_map, *sb_map, *rb_map;
    VkBuffer vb = make_buffer(64, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &vb_map);
    const float big[6] = { -1.0f, -1.0f, -0.5f, -1.0f, -1.0f, -0.5f };
    const float tiny[6] = { -1.0f, -1.0f, -0.953f, -1.0f, -1.0f, -0.953f };
    const float *tri = blend2 ? tiny : big;
    memcpy(vb_map, tri, sizeof(big));
    VkBuffer sb = make_buffer(64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &sb_map);
    memset(sb_map, 0, 64);
    VkBuffer rb = make_buffer((W * H + BLEND_ITER) * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &rb_map);

    /* Graphics pipelines (two identical objects, so binding alternates) */
    VkShaderModule vs = load_module(argv[2]), fs = load_module(argv[3]), cs = load_module(argv[4]);
    VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT, 0, 16 };
    VkPipelineLayoutCreateInfo gli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr };
    VkPipelineLayout gl;
    VK(vkCreatePipelineLayout(dev, &gli, NULL, &gl));
    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main" } };
    VkVertexInputBindingDescription vbd = { 0, 8, VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription vad = { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 };
    VkPipelineVertexInputStateCreateInfo vis = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vbd,
        .vertexAttributeDescriptionCount = 1, .pVertexAttributeDescriptions = &vad };
    VkPipelineInputAssemblyStateCreateInfo ias = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkViewport vp = { 0, 0, W, H, 0, 1 };
    VkRect2D scissor = { { 0, 0 }, { W, H } };
    VkPipelineViewportStateCreateInfo vps = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &scissor };
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xf };
    VkPipelineColorBlendStateCreateInfo cbs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cba };
    VkFormat cfmt = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo pri = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1, .pColorAttachmentFormats = &cfmt };
    VkGraphicsPipelineCreateInfo gpi = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &pri,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vis, .pInputAssemblyState = &ias,
        .pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &ms, .pColorBlendState = &cbs,
        .layout = gl };
    VkPipeline gp[2];
    VK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpi, NULL, &gp[0]));
    VK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpi, NULL, &gp[1]));
    VkPipelineColorBlendAttachmentState add = { .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE, .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = 0xf };
    cbs.pAttachments = &add;
    VkPipeline bp;
    VK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpi, NULL, &bp));
    static const float clears[3][4] = { { 0.0f, 0.2f, 0.4f, 1.0f }, { 0.0f, 0.6f, 0.1f, 1.0f }, { 0.0f, 0.0f, 0.9f, 1.0f } };

    /* Compute pipeline */
    VkDescriptorSetLayoutBinding lb = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL };
    VkDescriptorSetLayoutCreateInfo li = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &lb };
    VkDescriptorSetLayout dsl;
    VK(vkCreateDescriptorSetLayout(dev, &li, NULL, &dsl));
    VkPipelineLayoutCreateInfo cli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl };
    VkPipelineLayout cl;
    VK(vkCreatePipelineLayout(dev, &cli, NULL, &cl));
    VkComputePipelineCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                   .module = cs, .pName = "main" }, .layout = cl };
    VkPipeline cp;
    VK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpi, NULL, &cp));
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
    VkDescriptorPool dpool;
    VK(vkCreateDescriptorPool(dev, &dpi, NULL, &dpool));
    VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dpool, .descriptorSetCount = 1, .pSetLayouts = &dsl };
    VkDescriptorSet set;
    VK(vkAllocateDescriptorSets(dev, &dai, &set));
    VkDescriptorBufferInfo dbi = { sb, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet wds = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0,
        .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi };
    vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);

    VkQueryPoolCreateInfo qpi = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 };
    VkQueryPool qp;
    VK(vkCreateQueryPool(dev, &qpi, NULL, &qp));
    VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 };
    VkCommandPool pool;
    VK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cb;
    VK(vkAllocateCommandBuffers(dev, &cai, &cb));
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    VK(vkCreateFence(dev, &fci, NULL, &fence));

    const VkClearValue clear = { .color = { .float32 = { 0.0f, 0.0f, 1.0f, 1.0f } } };
    VkRenderingAttachmentInfo att = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = clear };
    VkRenderingInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = { { 0, 0 }, { W, H } },
        .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &att };
    const float off[4] = { 0, 0, 0, 0 };
    const VkDeviceSize zero = 0;

    uint64_t gpu[REPS], cpu[REPS];
    for (int rep = 0; rep < REPS + 1; rep++) {   /* rep 0 is warm-up */
        VK(vkResetCommandPool(dev, pool, 0));
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
        const uint64_t t0 = now_ns();
        VK(vkBeginCommandBuffer(cb, &bi));
        vkCmdResetQueryPool(cb, qp, 0, 2);
        /* Fast clear at the start of every submission. */
        image_barrier(cb, img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        if (blend2)
            memcpy(att.clearValue.color.float32, clears[rep % 3], sizeof(clears[0]));
        vkCmdBeginRendering(cb, &ri);
        vkCmdEndRendering(cb);
        att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, qp, 0);

        if (push)
            vkCmdBeginRendering(cb, &ri);
        for (int i = 0; i < ITER; i++) {
            if (blend) {
                if (i >= BLEND_ITER) break;
                att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                memcpy(att.clearValue.color.float32, clears[i % 3], sizeof(clears[0]));
                vkCmdBeginRendering(cb, &ri);
                vkCmdEndRendering(cb);
                att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                vkCmdBeginRendering(cb, &ri);
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, bp);
                vkCmdBindVertexBuffers(cb, 0, 1, &vb, &zero);
                vkCmdPushConstants(cb, gl, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, off);
                vkCmdDraw(cb, 3, 1, 0, 0);
                vkCmdEndRendering(cb);
                image_barrier(cb, img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
                VkBufferImageCopy px1 = { .bufferOffset = (W * H + i) * 4ull,
                    .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                    .imageOffset = { 1, 1, 0 }, .imageExtent = { 1, 1, 1 } };
                vkCmdCopyImageToBuffer(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb, 1, &px1);
                image_barrier(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
                continue;
            }
            if (blend2) {
                const float cell[4] = { (i % 32) * 0.0625f, (i / 32) * 0.0625f, 0, 0 };
                vkCmdBeginRendering(cb, &ri);
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, bp);
                vkCmdBindVertexBuffers(cb, 0, 1, &vb, &zero);
                vkCmdPushConstants(cb, gl, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, cell);
                vkCmdDraw(cb, 3, 1, 0, 0);
                vkCmdEndRendering(cb);
                continue;
            }
            if (vf) {
                float x = -1.0f;
                vkCmdUpdateBuffer(cb, vb, 0, 4, &x);
                memory_barrier(cb, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                               VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
            }
            if (vfe) {
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cp);
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cl, 0, 1, &set, 0, NULL);
                vkCmdDispatch(cb, 1, 1, 1);
            }
            if (!push)
                vkCmdBeginRendering(cb, &ri);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gp[push ? (i & 1) : 0]);
            vkCmdBindVertexBuffers(cb, 0, 1, &vb, &zero);
            vkCmdPushConstants(cb, gl, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, off);
            vkCmdDraw(cb, 3, 1, 0, 0);
            if (!push)
                vkCmdEndRendering(cb);
        }
        if (push)
            vkCmdEndRendering(cb);

        vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, qp, 1);
        image_barrier(cb, img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy r = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { W, H, 1 } };
        vkCmdCopyImageToBuffer(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb, 1, &r);
        memory_barrier(cb, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        VK(vkEndCommandBuffer(cb));
        const uint64_t t1 = now_ns();

        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
        VK(vkResetFences(dev, 1, &fence));
        memset(rb_map, 0x55, (W * H + BLEND_ITER) * 4);
        VK(vkQueueSubmit(queue, 1, &si, fence));
        VK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull));
        uint64_t ts[2];
        VK(vkGetQueryPoolResults(dev, qp, 0, 2, sizeof(ts), ts, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

        /* Read-back check */
        const uint32_t *px = rb_map;
        const uint32_t red = 0xff0000ffu, blue = 0xffff0000u;
        if (blend) {
            unsigned bad = 0;
            for (int i = 0; i < BLEND_ITER; i++) {
                const uint32_t g = (uint32_t)(clears[i % 3][1] * 255.0f + 0.5f), b = (uint32_t)(clears[i % 3][2] * 255.0f + 0.5f);
                const uint32_t want = 0xff000000u | (b << 16) | (g << 8) | 0xffu;
                uint32_t got = px[W * H + i];
                /* Allow one 8-bit step of rounding per channel. */
                int ok = 1;
                for (int ch = 0; ch < 4; ch++) {
                    int d = (int)((got >> (8 * ch)) & 0xff) - (int)((want >> (8 * ch)) & 0xff);
                    if (d < -1 || d > 1) ok = 0;
                }
                if (!ok) {
                    if (bad < 3) printf("    iteration %d: got %08x want %08x\n", i, got, want);
                    bad++;
                }
            }
            if (bad) { printf("blend pixels=BAD %u/%d iterations wrong\n", bad, BLEND_ITER); return 1; }
        } else if (blend2) {
            const float *c = clears[rep % 3];
            const uint32_t g = (uint32_t)(c[1] * 255.0f + 0.5f), b = (uint32_t)(c[2] * 255.0f + 0.5f);
            const uint32_t drawn = 0xff000000u | (b << 16) | (g << 8) | 0xffu;
            const uint32_t untouched = 0xff000000u | (b << 16) | (g << 8);
            unsigned bad = 0;
            for (int i = 0; i < 1024; i++) {
                const int x = (i % 32) * 8 + 1, y = (i / 32) * 8 + 1;
                const uint32_t want = i < ITER ? drawn : untouched, got = px[y * W + x];
                int ok = 1;
                for (int ch = 0; ch < 4; ch++) {
                    int d = (int)((got >> (8 * ch)) & 0xff) - (int)((want >> (8 * ch)) & 0xff);
                    if (d < -1 || d > 1) ok = 0;
                }
                if (!ok) {
                    if (bad < 3) printf("    rep %d cell %d: got %08x want %08x\n", rep, i, got, want);
                    bad++;
                }
            }
            if (bad) { printf("blend2 pixels=BAD %u/1024 cells wrong in rep %d\n", bad, rep); return 1; }
        } else if (px[1 * W + 1] != red || px[(H - 1) * W + (W - 1)] != blue || px[(H / 2) * W + W / 2] != blue) {
            printf("%s pixels=BAD inside=%08x outside=%08x center=%08x\n", sc,
                   px[1 * W + 1], px[(H - 1) * W + (W - 1)], px[(H / 2) * W + W / 2]);
            return 1;
        }
        if (rep > 0) {
            gpu[rep - 1] = (uint64_t)((ts[1] - ts[0]) * (double)props.limits.timestampPeriod);
            cpu[rep - 1] = t1 - t0;
        }
    }
    qsort(gpu, REPS, sizeof(gpu[0]), cmp_u64);
    qsort(cpu, REPS, sizeof(cpu[0]), cmp_u64);
    printf("%s %.1f %.1f pixels=ok\n", sc, gpu[REPS / 2] / (double)ITER, cpu[REPS / 2] / (double)ITER);
    vkDeviceWaitIdle(dev);
    return 0;
}
