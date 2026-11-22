/* Gfx9 VF cache aliasing across vkCmdExecuteCommands (TODO item 24).
 *
 * Two vertex buffers hold the same full-screen triangle, A coloured red and
 * B green, placed exactly 4 GiB apart in the GPU address space with
 * capture-replay addresses.  The Gfx9 VF cache tags only the low 32 bits,
 * so B can hit lines cached for A unless the driver invalidates between
 * them.  Each case draws A then B in back-to-back rendering scopes and
 * checks that the centre pixel is green:
 *
 *   entry  A in the primary, B in a secondary
 *   exit   A in a secondary, B in the primary
 *   inline A and B both in the primary (control, existing tracking)
 *
 * usage: vf-alias color.vert.spv color.frag.spv [repeats]
 * Exit 0 if every case was green every time, 1 on a stale (red) result,
 * 2 on setup failure, 3 if the 4 GiB placement was not possible.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define VK(call) do { VkResult rc_ = (call); if (rc_ != VK_SUCCESS) { \
    fprintf(stderr, "%s:%d %s returned %d\n", __FILE__, __LINE__, #call, rc_); exit(2); } } while (0)

enum { W = 64, H = 64, MEM = 1 << 16 };
static VkDevice dev;
static VkPhysicalDeviceMemoryProperties mp;

static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t t = 0; t < mp.memoryTypeCount; t++)
        if ((bits & (1u << t)) && (mp.memoryTypes[t].propertyFlags & want) == want) return t;
    exit(2);
}

static VkShaderModule module(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) exit(2);
    static uint32_t code[1 << 14];
    size_t n = fread(code, 1, sizeof(code), f);
    fclose(f);
    VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = n, .pCode = code };
    VkShaderModule m;
    VK(vkCreateShaderModule(dev, &ci, NULL, &m));
    return m;
}

/* Vertex buffer at a requested opaque capture address (0 = any). */
/* idx_base >= 0: fill as a uint16 index buffer {base, base+1, base+2}. */
static int idx_base = -1;
/* six_vertices: write vertices 3..5 green after the requested colour. */
static int six_vertices = 0;
static VkBuffer vertex_buffer(uint64_t want_addr, uint64_t *got_addr, const float rgba[4], VkResult *res)
{
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = MEM,
        .flags = VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT };
    VkBuffer b;
    VK(vkCreateBuffer(dev, &bi, NULL, &b));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, b, &req);
    VkMemoryOpaqueCaptureAddressAllocateInfo cap = { .sType = VK_STRUCTURE_TYPE_MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO,
        .opaqueCaptureAddress = want_addr };
    VkMemoryAllocateFlagsInfo fl = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .pNext = want_addr ? &cap : NULL,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT | VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT };
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &fl,
        .allocationSize = req.size,
        .memoryTypeIndex = mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    VkDeviceMemory m;
    *res = vkAllocateMemory(dev, &ai, NULL, &m);
    if (*res != VK_SUCCESS) { vkDestroyBuffer(dev, b, NULL); return VK_NULL_HANDLE; }
    VK(vkBindBufferMemory(dev, b, m, 0));
    VkDeviceMemoryOpaqueCaptureAddressInfo oi = { .sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_OPAQUE_CAPTURE_ADDRESS_INFO, .memory = m };
    *got_addr = vkGetDeviceMemoryOpaqueCaptureAddress(dev, &oi);
    float *v;
    VK(vkMapMemory(dev, m, 0, VK_WHOLE_SIZE, 0, (void **)&v));
    if (idx_base >= 0) {
        uint16_t *ix = (uint16_t *)v;
        for (int i = 0; i < 3; i++) ix[i] = (uint16_t)(idx_base + i);
        return b;
    }
    const float pos[3][2] = { { -1, -1 }, { 3, -1 }, { -1, 3 } };
    static const float green6[4] = { 0, 1, 0, 1 };
    for (int i = 0; i < (six_vertices ? 6 : 3); i++) {
        v[i * 6 + 0] = pos[i % 3][0]; v[i * 6 + 1] = pos[i % 3][1];
        memcpy(&v[i * 6 + 2], i < 3 ? rgba : green6, 16);
    }
    return b;
}

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    const int repeats = argc > 3 ? atoi(argv[3]) : 20;
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    VK(vkCreateInstance(&ii, NULL, &inst));
    uint32_t n = 1;
    VkPhysicalDevice phys;
    vkEnumeratePhysicalDevices(inst, &n, &phys);
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    VkPhysicalDeviceVulkan12Features f12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceFeatures2 f2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f12 };
    vkGetPhysicalDeviceFeatures2(phys, &f2);
    if (!f12.bufferDeviceAddressCaptureReplay) { fprintf(stderr, "no capture replay\n"); return 3; }

    VkPhysicalDeviceVulkan13Features e13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE, .synchronization2 = VK_TRUE };
    VkPhysicalDeviceVulkan12Features e12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &e13,
        .bufferDeviceAddress = VK_TRUE, .bufferDeviceAddressCaptureReplay = VK_TRUE };
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &e12,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi };
    VK(vkCreateDevice(phys, &di, NULL, &dev));
    VkQueue queue;
    vkGetDeviceQueue(dev, 0, 0, &queue);

    const float red[4] = { 1, 0, 0, 1 }, green[4] = { 0, 1, 0, 1 };
    uint64_t addr_a = 0, addr_b = 0;
    VkResult r;
    VkBuffer vb_a = vertex_buffer(0, &addr_a, red, &r);
    if (!vb_a) return 2;
    VkBuffer vb_b = VK_NULL_HANDLE;
    for (uint64_t k = 1; k <= 8 && !vb_b; k++)
        vb_b = vertex_buffer(addr_a + (k << 32), &addr_b, green, &r);
    if (!vb_b || (uint32_t)addr_a != (uint32_t)addr_b) {
        printf("could not place B at A + n*4GiB (A=0x%llx)\n", (unsigned long long)addr_a);
        return 3;
    }
    printf("A at 0x%llx, B at 0x%llx (low 32 bits equal)\n", (unsigned long long)addr_a, (unsigned long long)addr_b);

    /* Target and readback */
    VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
    VkImage img;
    VK(vkCreateImage(dev, &ici, NULL, &img));
    VkMemoryRequirements ireq;
    vkGetImageMemoryRequirements(dev, img, &ireq);
    VkMemoryAllocateInfo iai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = ireq.size,
        .memoryTypeIndex = mem_type(ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    VkDeviceMemory imem;
    VK(vkAllocateMemory(dev, &iai, NULL, &imem));
    VK(vkBindImageMemory(dev, img, imem, 0));
    VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VkImageView view;
    VK(vkCreateImageView(dev, &vci, NULL, &view));
    VkBufferCreateInfo rbi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = W * H * 4,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    VkBuffer rb;
    VK(vkCreateBuffer(dev, &rbi, NULL, &rb));
    VkMemoryRequirements rreq;
    vkGetBufferMemoryRequirements(dev, rb, &rreq);
    VkMemoryAllocateInfo rai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = rreq.size,
        .memoryTypeIndex = mem_type(rreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    VkDeviceMemory rmem;
    VK(vkAllocateMemory(dev, &rai, NULL, &rmem));
    VK(vkBindBufferMemory(dev, rb, rmem, 0));
    uint32_t *px;
    VK(vkMapMemory(dev, rmem, 0, VK_WHOLE_SIZE, 0, (void **)&px));

    /* Pipeline */
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkPipelineLayout pl;
    VK(vkCreatePipelineLayout(dev, &pli, NULL, &pl));
    VkPipelineShaderStageCreateInfo st[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = module(argv[1]), .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = module(argv[2]), .pName = "main" } };
    VkVertexInputBindingDescription vbd = { 0, 24, VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription vad[2] = { { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8 } };
    VkPipelineVertexInputStateCreateInfo vis = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vbd,
        .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = vad };
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
        .pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &msi, .pColorBlendState = &cbs, .layout = pl };
    VkPipeline pipe;
    VK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpi, NULL, &pipe));

    VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandPool pool;
    VK(vkCreateCommandPool(dev, &cpi, NULL, &pool));
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    VK(vkCreateFence(dev, &fci, NULL, &fence));

    VkRenderingAttachmentInfo att = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
    VkRenderingInfo ri_inline = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = sc,
        .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &att };
    VkRenderingInfo ri_sec = ri_inline;
    ri_sec.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
    const VkDeviceSize zero = 0;

    if (getenv("IDX")) {
        /* Index buffer aliasing: one vertex buffer with a red triangle
         * (0..2) and a green one (3..5); index buffers A={0,1,2} and
         * B={3,4,5} 4 GiB apart.  Clean batch, one scope: draw A then B. */
        uint64_t va = 0, ia = 0, ib = 0;
        six_vertices = 1;
        VkBuffer vb6 = vertex_buffer(0, &va, red, &r);
        six_vertices = 0;
        float *vm;
        VkDeviceMemory dummy; (void)dummy;
        /* rewrite vb6: vertices 3..5 green */
        {
            VkMemoryRequirements q; vkGetBufferMemoryRequirements(dev, vb6, &q); (void)q;
        }
        idx_base = 0;
        VkBuffer ib_a = vertex_buffer(0, &ia, red, &r);
        VkBuffer ib_b = VK_NULL_HANDLE;
        idx_base = 3;
        for (uint64_t k = 1; k <= 8 && !ib_b; k++)
            ib_b = vertex_buffer(ia + (k << 32), &ib, red, &r);
        idx_base = -1;
        if (!ib_b || (uint32_t)ia != (uint32_t)ib) { printf("could not place index buffers\n"); return 3; }
        printf("IB A at 0x%llx, IB B at 0x%llx\n", (unsigned long long)ia, (unsigned long long)ib);
        (void)vm;
        int bad = 0;
        const int reps = argc > 3 ? atoi(argv[3]) : 20;
        for (int rep = 0; rep < reps; rep++) {
            VK(vkResetCommandPool(dev, pool, 0));
            VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
            VkCommandBuffer pre, prim;
            VK(vkAllocateCommandBuffers(dev, &cai, &pre));
            VK(vkAllocateCommandBuffers(dev, &cai, &prim));
            VkCommandBufferBeginInfo pbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            VkImageMemoryBarrier2 b0 = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .image = img,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
            VkDependencyInfo d0 = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b0 };
            VK(vkBeginCommandBuffer(pre, &pbi));
            vkCmdPipelineBarrier2(pre, &d0);
            VK(vkEndCommandBuffer(pre));
            VkSubmitInfo si0 = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &pre };
            VK(vkQueueSubmit(queue, 1, &si0, VK_NULL_HANDLE));
            VK(vkQueueWaitIdle(queue));
            VK(vkBeginCommandBuffer(prim, &pbi));
            vkCmdBeginRendering(prim, &ri_inline);
            vkCmdBindPipeline(prim, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            vkCmdBindVertexBuffers(prim, 0, 1, &vb6, &zero);
            vkCmdBindIndexBuffer(prim, ib_a, 0, VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(prim, 3, 1, 0, 0, 0);
            vkCmdBindIndexBuffer(prim, ib_b, 0, VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(prim, 3, 1, 0, 0, 0);
            vkCmdEndRendering(prim);
            VkImageMemoryBarrier2 b1 = b0;
            b1.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT; b1.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b1.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT; b1.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            b1.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            VkDependencyInfo d1 = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b1 };
            vkCmdPipelineBarrier2(prim, &d1);
            VkBufferImageCopy region = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { W, H, 1 } };
            vkCmdCopyImageToBuffer(prim, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb, 1, &region);
            VK(vkEndCommandBuffer(prim));
            VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &prim };
            VK(vkResetFences(dev, 1, &fence));
            VK(vkQueueSubmit(queue, 1, &si, fence));
            VK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull));
            uint32_t center = px[(H / 2) * W + W / 2];
            if (center != 0xff00ff00u) { if (bad < 2) printf("    rep %d: centre %08x\n", rep, center); bad++; }
        }
        printf("index buffers (A then B, clean): %d/%d stale\n", bad, reps);
        return bad ? 1 : 0;
    }

    if (getenv("BENCH")) {
        /* 1000 executions of one secondary (one draw from A) inside one
         * rendering scope, bracketed by timestamps; median of 9. */
        VkQueryPoolCreateInfo qpi = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 };
        VkQueryPool qp;
        VK(vkCreateQueryPool(dev, &qpi, NULL, &qp));
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(phys, &props);
        uint64_t ns[9];
        for (int rep = 0; rep < 10; rep++) {
            VK(vkResetCommandPool(dev, pool, 0));
            VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                .level = VK_COMMAND_BUFFER_LEVEL_SECONDARY, .commandBufferCount = 1 };
            VkCommandBuffer sec, prim;
            VK(vkAllocateCommandBuffers(dev, &cai, &sec));
            cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            VK(vkAllocateCommandBuffers(dev, &cai, &prim));
            VkCommandBufferInheritanceRenderingInfo inh_r = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
                .colorAttachmentCount = 1, .pColorAttachmentFormats = &cf, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
            VkCommandBufferInheritanceInfo inh = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO, .pNext = &inh_r };
            VkCommandBufferBeginInfo sbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
                .pInheritanceInfo = &inh };
            VK(vkBeginCommandBuffer(sec, &sbi));
            vkCmdBindPipeline(sec, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            vkCmdBindVertexBuffers(sec, 0, 1, &vb_a, &zero);
            vkCmdDraw(sec, 3, 1, 0, 0);
            VK(vkEndCommandBuffer(sec));
            VkCommandBufferBeginInfo pbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            VK(vkBeginCommandBuffer(prim, &pbi));
            vkCmdResetQueryPool(prim, qp, 0, 2);
            VkImageMemoryBarrier2 b0 = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .image = img,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
            VkDependencyInfo d0 = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b0 };
            vkCmdPipelineBarrier2(prim, &d0);
            vkCmdWriteTimestamp2(prim, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, qp, 0);
            vkCmdBeginRendering(prim, &ri_sec);
            for (int i = 0; i < 1000; i++)
                vkCmdExecuteCommands(prim, 1, &sec);
            vkCmdEndRendering(prim);
            vkCmdWriteTimestamp2(prim, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, qp, 1);
            VK(vkEndCommandBuffer(prim));
            VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &prim };
            VK(vkResetFences(dev, 1, &fence));
            VK(vkQueueSubmit(queue, 1, &si, fence));
            VK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull));
            uint64_t ts[2];
            VK(vkGetQueryPoolResults(dev, qp, 0, 2, sizeof(ts), ts, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
            if (rep) ns[rep - 1] = (uint64_t)((ts[1] - ts[0]) * (double)props.limits.timestampPeriod);
        }
        for (int i = 0; i < 9; i++) for (int j = i + 1; j < 9; j++) if (ns[j] < ns[i]) { uint64_t t = ns[i]; ns[i] = ns[j]; ns[j] = t; }
        printf("bench: 1000 vkCmdExecuteCommands, GPU median %.2f us per execution\n", ns[4] / 1000.0 / 1000.0);
        return 0;
    }

    const char *names[4] = { "entry  (A primary, B secondary)", "exit   (A secondary, B primary)", "inline (A and B in primary)",
                             "clean  (A then B, one scope, no transition in batch)" };
    int failures = 0;
    for (int c = 0; c < 4; c++) {
        if (getenv("ONLY") && atoi(getenv("ONLY")) != c) continue;
        int bad = 0;
        for (int rep = 0; rep < repeats; rep++) {
            VK(vkResetCommandPool(dev, pool, 0));
            VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
            VkCommandBuffer prim, sec;
            VK(vkAllocateCommandBuffers(dev, &cai, &prim));
            cai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
            VK(vkAllocateCommandBuffers(dev, &cai, &sec));

            /* Secondary: one draw from the buffer the case assigns to it. */
            VkCommandBufferInheritanceRenderingInfo inh_r = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
                .colorAttachmentCount = 1, .pColorAttachmentFormats = &cf, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
            VkCommandBufferInheritanceInfo inh = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO, .pNext = &inh_r };
            VkCommandBufferBeginInfo sbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT, .pInheritanceInfo = &inh };
            VK(vkBeginCommandBuffer(sec, &sbi));
            vkCmdBindPipeline(sec, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            vkCmdBindVertexBuffers(sec, 0, 1, c == 0 ? &vb_b : &vb_a, &zero);
            vkCmdDraw(sec, 3, 1, 0, 0);
            VK(vkEndCommandBuffer(sec));

            VkCommandBufferBeginInfo pbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            VK(vkBeginCommandBuffer(prim, &pbi));
            VkImageMemoryBarrier2 b0 = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .image = img,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
            VkDependencyInfo d0 = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b0 };
            if (c == 3) {
                /* Transition in its own submission so this batch has no
                 * BLORP or other VB-range activity before the two draws. */
                VK(vkEndCommandBuffer(prim));
                VkCommandBuffer pre;
                VkCommandBufferAllocateInfo cai2 = cai;
                cai2.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                VK(vkAllocateCommandBuffers(dev, &cai2, &pre));
                VK(vkAllocateCommandBuffers(dev, &cai2, &prim));
                VK(vkBeginCommandBuffer(pre, &pbi));
                vkCmdPipelineBarrier2(pre, &d0);
                VkRenderingAttachmentInfo clr = att;
                clr.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                VkRenderingInfo rc = ri_inline;
                rc.pColorAttachments = &clr;
                vkCmdBeginRendering(pre, &rc);
                vkCmdEndRendering(pre);
                VK(vkEndCommandBuffer(pre));
                VkSubmitInfo si0 = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &pre };
                VK(vkQueueSubmit(queue, 1, &si0, VK_NULL_HANDLE));
                VK(vkQueueWaitIdle(queue));
                VK(vkBeginCommandBuffer(prim, &pbi));
                vkCmdBeginRendering(prim, &ri_inline);
                vkCmdBindPipeline(prim, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                vkCmdBindVertexBuffers(prim, 0, 1, &vb_a, &zero);
                vkCmdDraw(prim, 3, 1, 0, 0);
                vkCmdBindVertexBuffers(prim, 0, 1, &vb_b, &zero);
                vkCmdDraw(prim, 3, 1, 0, 0);
                vkCmdEndRendering(prim);
            } else
            vkCmdPipelineBarrier2(prim, &d0);

            for (int step = 0; step < 2 && c != 3; step++) {
                /* step 0 draws A, step 1 draws B */
                const int in_secondary = (c == 0 && step == 1) || (c == 1 && step == 0);
                if (in_secondary) {
                    vkCmdBeginRendering(prim, &ri_sec);
                    vkCmdExecuteCommands(prim, 1, &sec);
                } else {
                    vkCmdBeginRendering(prim, &ri_inline);
                    vkCmdBindPipeline(prim, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                    vkCmdBindVertexBuffers(prim, 0, 1, step == 0 ? &vb_a : &vb_b, &zero);
                    vkCmdDraw(prim, 3, 1, 0, 0);
                }
                vkCmdEndRendering(prim);
            }
            VkImageMemoryBarrier2 b1 = b0;
            b1.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            b1.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            b1.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            b1.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            b1.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            VkDependencyInfo d1 = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b1 };
            vkCmdPipelineBarrier2(prim, &d1);
            VkBufferImageCopy region = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { W, H, 1 } };
            vkCmdCopyImageToBuffer(prim, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb, 1, &region);
            VK(vkEndCommandBuffer(prim));
            VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &prim };
            VK(vkResetFences(dev, 1, &fence));
            VK(vkQueueSubmit(queue, 1, &si, fence));
            VK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull));
            const uint32_t center = px[(H / 2) * W + W / 2];
            if (center != 0xff00ff00u) {
                if (bad < 2) printf("    %s rep %d: centre %08x (expected green ff00ff00)\n", names[c], rep, center);
                bad++;
            }
        }
        printf("%s: %d/%d stale\n", names[c], bad, repeats);
        failures += bad;
    }
    printf("%s\n", failures ? "FAIL" : "PASS");
    vkDeviceWaitIdle(dev);
    return failures ? 1 : 0;
}
