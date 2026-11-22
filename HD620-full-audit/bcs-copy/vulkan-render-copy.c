/* Render-engine comparison for the BCS copy fixture: vkCmdCopyBuffer on ANV's existing
 * general queue family (Gen9 ANV executes it on the render engine; no transfer-only family
 * is exposed and none is created here). Same sizes, layout, per-iteration data, canaries,
 * full verification, modes and units as i915-bcs-copy.c.
 *
 * Uses only the public Vulkan API on whichever ICD VK_DRIVER_FILES selects; refuses any
 * device other than 0x8086:0x5916. Waits are bounded fences. Engine time comes from
 * /proc/self/fdinfo of the driver's own render-node fd(s). */
#include "bcs-common.h"

#include <getopt.h>
#include <vulkan/vulkan.h>

#define VK(call) do { VkResult rc_ = (call); if (rc_ != VK_SUCCESS) { \
    fprintf(stderr, "%s returned %d\n", #call, rc_); exit(EXIT_GPU); } } while (0)

#define MAX_PROGRAMS 256u

enum mode { MODE_LATENCY, MODE_QUEUED, MODE_BATCH };
static const char *mode_name[] = { "latency", "queued", "batch" };

static VkPhysicalDevice gpu;
static VkDevice device;
static VkQueue queue;
static uint32_t family, memtype;
static VkCommandPool pool;
static double ts_period;
static uint64_t ts_mask;
static uint64_t timeout_ns = 2000000000ull;
static int failures;

struct buffer { VkBuffer buf; VkDeviceMemory mem; uint8_t *map; VkDeviceSize bytes; };

static struct buffer buffer_new(VkDeviceSize bytes)
{
    struct buffer b = { .bytes = bytes };
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VK(vkCreateBuffer(device, &bi, NULL, &b.buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, b.buf, &req);
    if (!(req.memoryTypeBits & (1u << memtype))) {
        fprintf(stderr, "buffer cannot use the selected host-visible memory type\n");
        exit(EXIT_DEVICE);
    }
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = memtype };
    VK(vkAllocateMemory(device, &ai, NULL, &b.mem));
    VK(vkBindBufferMemory(device, b.buf, b.mem, 0));
    VK(vkMapMemory(device, b.mem, 0, VK_WHOLE_SIZE, 0, (void **)&b.map));
    return b;
}

static void buffer_free(struct buffer *b)
{
    vkUnmapMemory(device, b->mem);
    vkDestroyBuffer(device, b->buf, NULL);
    vkFreeMemory(device, b->mem, NULL);
}

static void report_mismatch(const char *mode, uint64_t size, unsigned it,
                            const struct verify_result *r)
{
    printf("{\"record\":\"mismatch\",\"route\":\"render-vk\",\"mode\":\"%s\",\"size_bytes\":%"
           PRIu64 ",\"iteration\":%u,\"bad_words\":%" PRIu64 ",\"first\":\"%s\"}\n",
           mode, size, it, r->bad_words, r->first);
    failures++;
}

struct config { unsigned iters, warmup, queue, batch, verify_only; uint64_t max_working_set; };

/* Command buffer p: reset its two queries, stamp, copy slots [first, first+count), stamp at
 * BOTTOM_OF_PIPE, then make transfer writes available to the host. Recorded once and
 * resubmitted, so timing excludes recording. */
static VkCommandBuffer record(VkQueryPool qp, unsigned p, struct buffer *src, struct buffer *dst,
                              uint64_t size, unsigned first, unsigned count)
{
    VkCommandBufferAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cb;
    VK(vkAllocateCommandBuffers(device, &ai, &cb));
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VK(vkBeginCommandBuffer(cb, &bi));
    vkCmdResetQueryPool(cb, qp, 2 * p, 2);
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qp, 2 * p);
    for (unsigned s = first; s < first + count; s++) {
        VkBufferCopy region = { slot_offset(size, s), slot_offset(size, s), size };
        vkCmdCopyBuffer(cb, src->buf, dst->buf, 1, &region);
    }
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 2 * p + 1);
    VkMemoryBarrier host = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                         &host, 0, NULL, 0, NULL);
    VK(vkEndCommandBuffer(cb));
    return cb;
}

static void run(enum mode m, uint64_t size, const struct config *c)
{
    uint64_t cap = c->max_working_set / size;
    unsigned slots = m == MODE_QUEUED ? c->queue : m == MODE_BATCH ? c->batch : 1;
    if (cap < 1) cap = 1;
    if (slots > cap) slots = (unsigned)cap;
    unsigned progs = m == MODE_QUEUED ? slots : 1;
    struct buffer src = buffer_new(layout_bytes(size, slots));
    struct buffer dst = buffer_new(layout_bytes(size, slots));
    VkQueryPoolCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 * progs };
    VkQueryPool qp;
    VK(vkCreateQueryPool(device, &qi, NULL, &qp));
    VkCommandBuffer cbs[MAX_PROGRAMS];
    for (unsigned p = 0; p < progs; p++)
        cbs[p] = m == MODE_QUEUED ? record(qp, p, &src, &dst, size, p, 1)
                                  : record(qp, 0, &src, &dst, size, 0, slots);
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    VK(vkCreateFence(device, &fi, NULL, &fence));

    unsigned n = c->iters;
    double *host = calloc(n, sizeof(double)), *submit = calloc(n, sizeof(double));
    double *gpu_span = calloc(n, sizeof(double)), *gpu_sum = calloc(n, sizeof(double));
    struct verify_result vr = {0};
    struct engine_ns e0 = {0};
    uint64_t route = 0x564b52, wall0 = 0;
    double host_total = 0;

    for (unsigned it = 0; it < c->warmup + n; it++) {
        if (it == c->warmup) {
            e0 = process_engine_ns();
            wall0 = now_ns();
        }
        /* HOST_COHERENT memory: host writes before vkQueueSubmit are visible to the device. */
        prepare_buffers(src.map, dst.map, size, slots, route, it);
        VK(vkResetFences(device, 1, &fence));
        uint64_t sub = 0, t0 = now_ns();
        for (unsigned p = 0; p < progs; p++) {
            VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1, .pCommandBuffers = &cbs[p] };
            uint64_t a = now_ns();
            VK(vkQueueSubmit(queue, 1, &si, p + 1 == progs ? fence : VK_NULL_HANDLE));
            sub += now_ns() - a;
        }
        VkResult w = vkWaitForFences(device, 1, &fence, VK_TRUE, timeout_ns);
        uint64_t t1 = now_ns();
        if (w != VK_SUCCESS) {
            fprintf(stderr, "bounded fence wait returned %d after %" PRIu64 " ns; exiting\n", w,
                    timeout_ns);
            exit(EXIT_GPU);
        }
        uint64_t q[2 * MAX_PROGRAMS];
        VK(vkGetQueryPoolResults(device, qp, 0, 2 * progs, sizeof(uint64_t) * 2 * progs, q,
                                 sizeof(uint64_t), VK_QUERY_RESULT_64_BIT));
        double gsum = 0;
        for (unsigned p = 0; p < progs; p++)
            gsum += (double)((q[2 * p + 1] - q[2 * p]) & ts_mask);
        double span = (double)((q[2 * progs - 1] - q[0]) & ts_mask);
        struct verify_result r = {0};
        verify_buffers(src.map, dst.map, size, slots, route, it, &r);
        if (r.bad_words)
            report_mismatch(mode_name[m], size, it, &r);
        vr.bytes_checked += r.bytes_checked;
        vr.bad_words += r.bad_words;
        if (it >= c->warmup) {
            unsigned k = it - c->warmup;
            host[k] = (double)(t1 - t0);
            submit[k] = (double)sub;
            gpu_span[k] = span * ts_period;
            gpu_sum[k] = gsum * ts_period;
            host_total += host[k];
        }
    }
    uint64_t wall = now_ns() - wall0;
    struct engine_ns e1 = process_engine_ns();
    uint64_t render_ns = e1.render - e0.render, copy_ns = e1.copy - e0.copy;
    double bytes = (double)size * slots;
    struct stats hs = summarize(host, n), gs = summarize(gpu_span, n);

    printf("{\"record\":\"%s\",\"route\":\"render-vk\",\"packet\":\"vkCmdCopyBuffer\","
           "\"mode\":\"%s\",\"size_bytes\":%" PRIu64 ",\"copies_per_sample\":%u,"
           "\"submissions_per_sample\":%u,\"bytes_per_sample\":%" PRIu64
           ",\"working_set_bytes\":%" PRIu64 ",\"warmup\":%u,\"samples\":%u",
           c->verify_only ? "verify" : "result", mode_name[m], size, slots, progs,
           size * slots, 2 * layout_bytes(size, slots), c->warmup, n);
    json_stats(stdout, "host_ns", host, n);
    json_stats(stdout, "submit_call_ns", submit, n);
    json_stats(stdout, "gpu_span_ns", gpu_span, n);
    json_stats(stdout, "gpu_program_sum_ns", gpu_sum, n);
    printf(",\"host_GBps_p50\":%.3f,\"gpu_GBps_p50\":%.3f,\"host_GBps_aggregate\":%.3f"
           ",\"fdinfo_clients\":%d,\"fdinfo_render_ns\":%" PRIu64 ",\"fdinfo_copy_ns\":%" PRIu64
           ",\"render_busy_vs_host\":%.3f,\"phase_wall_ns\":%" PRIu64
           ",\"verified_bytes\":%" PRIu64 ",\"bad_words\":%" PRIu64 "}\n",
           gbps(bytes, hs.p50), gbps(bytes, gs.p50), gbps(bytes * n, host_total), e1.clients,
           render_ns, copy_ns, host_total > 0 ? render_ns / host_total : 0, wall,
           vr.bytes_checked, vr.bad_words);
    if (e1.clients && (render_ns == 0 || copy_ns != 0)) {
        fprintf(stderr, "engine accounting does not show a render-only run (render %" PRIu64
                " ns, copy %" PRIu64 " ns)\n", render_ns, copy_ns);
        failures++;
    }
    fflush(stdout);
    free(host);
    free(submit);
    free(gpu_span);
    free(gpu_sum);
    vkDestroyFence(device, fence, NULL);
    vkFreeCommandBuffers(device, pool, progs, cbs);
    vkDestroyQueryPool(device, qp, NULL);
    buffer_free(&dst);
    buffer_free(&src);
}

static int init(void)
{
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app };
    VkInstance instance;
    VK(vkCreateInstance(&ii, NULL, &instance));
    VkPhysicalDevice gpus[8];
    uint32_t count = 8;
    VkResult er = vkEnumeratePhysicalDevices(instance, &count, gpus);
    if (er != VK_SUCCESS && er != VK_INCOMPLETE)
        return EXIT_DEVICE;
    VkPhysicalDeviceProperties props;
    uint32_t i;
    for (i = 0; i < count; i++) {
        vkGetPhysicalDeviceProperties(gpus[i], &props);
        if (props.vendorID == 0x8086 && props.deviceID == KBL_GT2_HD620_DEVID)
            break;
    }
    if (i == count) {
        fprintf(stderr, "no 8086:%04x Vulkan device; refusing\n", KBL_GT2_HD620_DEVID);
        return EXIT_DEVICE;
    }
    gpu = gpus[i];
    if (!props.limits.timestampComputeAndGraphics) {
        fprintf(stderr, "timestamps unsupported\n");
        return EXIT_DEVICE;
    }
    ts_period = props.limits.timestampPeriod;

    VkQueueFamilyProperties fam[16];
    count = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, fam);
    for (family = 0; family < count; family++)
        if (fam[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            break;
    if (family == count || !fam[family].timestampValidBits)
        return EXIT_DEVICE;
    ts_mask = fam[family].timestampValidBits >= 64 ? ~0ull
                                                   : (1ull << fam[family].timestampValidBits) - 1;

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
    const VkMemoryPropertyFlags need = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    int best = -1;
    for (uint32_t t = 0; t < mp.memoryTypeCount; t++) {
        VkMemoryPropertyFlags f = mp.memoryTypes[t].propertyFlags;
        if ((f & need) != need)
            continue;
        if (best < 0 || (f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
            best = (int)t;
        if (f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
            break;
    }
    if (best < 0)
        return EXIT_DEVICE;
    memtype = (uint32_t)best;

    const float prio = 1;
    VkDeviceQueueCreateInfo qc = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo dc = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qc };
    VK(vkCreateDevice(gpu, &dc, NULL, &device));
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pc = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family };
    VK(vkCreateCommandPool(device, &pc, NULL, &pool));

    printf("{\"record\":\"header\",\"fixture\":\"vulkan-render-copy\",\"device\":\"%s\","
           "\"device_id\":\"0x%04x\",\"driver_version\":%u,\"queue_family\":%u,"
           "\"queue_flags\":\"0x%x\",\"memory_type\":%u,\"memory_flags\":\"0x%x\","
           "\"timestamp_period_ns\":%.3f,\"timestamp_valid_bits\":%u,\"timeout_ns\":%" PRIu64
           ",\"units\":\"ns; GB/s = 1e9 bytes/s; sizes in bytes\"}\n",
           props.deviceName, props.deviceID, props.driverVersion, family,
           fam[family].queueFlags, memtype, mp.memoryTypes[memtype].propertyFlags, ts_period,
           fam[family].timestampValidBits, timeout_ns);
    fflush(stdout);
    return EXIT_OK;
}

int main(int argc, char **argv)
{
    struct config c = { .iters = 50, .warmup = 5, .queue = 16, .batch = 16,
                        .max_working_set = 64ull << 20 };
    const char *modes = "latency,queued,batch", *size_arg = "4K,64K,1M,8M,64M";
    static const struct option opts[] = {
        { "verify", no_argument, 0, 'V' }, { "modes", required_argument, 0, 'm' },
        { "sizes", required_argument, 0, 's' }, { "iters", required_argument, 0, 'i' },
        { "warmup", required_argument, 0, 'w' }, { "queue", required_argument, 0, 'q' },
        { "batch", required_argument, 0, 'b' }, { "max-working-set", required_argument, 0, 'W' },
        { "timeout-ms", required_argument, 0, 't' }, { 0, 0, 0, 0 } };
    for (int o; (o = getopt_long(argc, argv, "", opts, NULL)) != -1;) {
        switch (o) {
        case 'V': c.verify_only = 1; break;
        case 'm': modes = optarg; break;
        case 's': size_arg = optarg; break;
        case 'i': c.iters = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'w': c.warmup = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'q': c.queue = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'b': c.batch = (unsigned)strtoul(optarg, NULL, 10); break;
        case 'W': c.max_working_set = strtoull(optarg, NULL, 0); break;
        case 't': timeout_ns = strtoull(optarg, NULL, 10) * 1000000ull; break;
        default:
            fprintf(stderr, "usage: %s [--verify] [--modes latency,queued,batch] [--sizes ...] "
                            "[--iters N] [--warmup N] [--queue K] [--batch N] "
                            "[--max-working-set B] [--timeout-ms MS]\n", argv[0]);
            return EXIT_USAGE;
        }
    }
    uint64_t sizes[MAX_SIZES];
    int nsizes = parse_sizes(c.verify_only ? "4K,8K,12K,1M,8M,8M+4K,24M+12K" : size_arg, sizes,
                             MAX_SIZES);
    if (c.verify_only) {
        c.iters = 3, c.warmup = 0, c.queue = 4, c.batch = 4, c.max_working_set = 128ull << 20;
        modes = "latency,queued,batch";
    }
    if (nsizes <= 0 || c.iters < 1 || c.iters > 10000 || c.warmup > 1000 || c.queue < 1 ||
        c.queue > MAX_PROGRAMS || c.batch < 1 || c.batch > MAX_PROGRAMS ||
        c.max_working_set < PAGE_BYTES || c.max_working_set > (512ull << 20) ||
        timeout_ns < 1000000 || timeout_ns > 10000000000ull) {
        fprintf(stderr, "invalid arguments (same limits as i915-bcs-copy)\n");
        return EXIT_USAGE;
    }
    int rc = init();
    if (rc)
        return rc;
    for (int i = 0; i < nsizes; i++)
        for (int m = 0; m < 3; m++)
            if (strstr(modes, mode_name[m]))
                run(m, sizes[i], &c);
    VK(vkDeviceWaitIdle(device));
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyDevice(device, NULL);
    printf("{\"record\":\"summary\",\"failures\":%d}\n", failures);
    return failures ? EXIT_MISMATCH : EXIT_OK;
}
