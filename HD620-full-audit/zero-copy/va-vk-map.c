/* VA-API -> Vulkan zero-copy mapping check through FFmpeg's public API.
 *
 * Uploads a seeded NV12 pattern to a VA-API surface, maps it into a Vulkan
 * frame, downloads the Vulkan frame and compares every byte.
 *
 *   A  av_hwframe_map(vulkan <- vaapi), what the hwmap filter does
 *   B  av_hwframe_map(drm_prime <- vaapi), then av_hwframe_map(vulkan <- drm)
 *
 * usage: va-vk-map [nv12|p010]
 * Exit 0 if every attempted route that maps also verifies; 1 on a pixel
 * mismatch; 2 on setup failure.  A route that fails to map is reported and
 * does not fail the run, so the output records which routes work.
 */
#include <stdio.h>
#include <string.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>

#define W 640
#define H 360

static int check(int ret, const char *what)
{
    if (ret < 0) {
        char buf[128];
        av_strerror(ret, buf, sizeof(buf));
        fprintf(stderr, "%s: %s (%d)\n", what, buf, ret);
    }
    return ret;
}

static void fill(AVFrame *f, int p010)
{
    uint32_t s = 0x1234567u;
    for (int p = 0; p < 2; p++) {
        const int rows = p ? H / 2 : H, bytes = W * (p010 ? 2 : 1);
        for (int y = 0; y < rows; y++) {
            uint8_t *row = f->data[p] + y * f->linesize[p];
            for (int x = 0; x < bytes; x++) {
                s = s * 1664525u + 1013904223u;
                row[x] = s >> 24;
            }
            /* P010 keeps data in the top 10 bits. */
            if (p010)
                for (int x = 0; x < bytes; x += 2)
                    row[x] &= 0xc0;
        }
    }
}

static long compare(const AVFrame *a, const AVFrame *b, int p010)
{
    long bad = 0;
    for (int p = 0; p < 2; p++) {
        const int rows = p ? H / 2 : H, bytes = W * (p010 ? 2 : 1);
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < bytes; x++)
                bad += a->data[p][y * a->linesize[p] + x] != b->data[p][y * b->linesize[p] + x];
    }
    return bad;
}

int main(int argc, char **argv)
{
    const int p010 = argc > 1 && !strcmp(argv[1], "p010");
    const enum AVPixelFormat sw = p010 ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
    AVBufferRef *va_dev = NULL, *vk_dev = NULL, *va_frames = NULL, *vk_frames = NULL;
    int failed = 0;

    if (check(av_hwdevice_ctx_create(&va_dev, AV_HWDEVICE_TYPE_VAAPI, "/dev/dri/renderD128", NULL, 0), "create vaapi") < 0 ||
        check(av_hwdevice_ctx_create_derived(&vk_dev, AV_HWDEVICE_TYPE_VULKAN, va_dev, 0), "derive vulkan") < 0)
        return 2;

    va_frames = av_hwframe_ctx_alloc(va_dev);
    AVHWFramesContext *vfc = (AVHWFramesContext *)va_frames->data;
    vfc->format = AV_PIX_FMT_VAAPI;
    vfc->sw_format = sw;
    vfc->width = W;
    vfc->height = H;
    vfc->initial_pool_size = 4;
    if (check(av_hwframe_ctx_init(va_frames), "init vaapi frames") < 0)
        return 2;
    /* Same derivation the hwmap filter performs. */
    if (check(av_hwframe_ctx_create_derived(&vk_frames, AV_PIX_FMT_VULKAN, vk_dev, va_frames,
                                            AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT), "derive vulkan frames") < 0)
        return 2;

    AVFrame *sw_in = av_frame_alloc(), *va = av_frame_alloc();
    sw_in->format = sw;
    sw_in->width = W;
    sw_in->height = H;
    if (check(av_frame_get_buffer(sw_in, 64), "sw alloc") < 0 ||
        check(av_hwframe_get_buffer(va_frames, va, 0), "vaapi surface") < 0)
        return 2;
    fill(sw_in, p010);
    if (check(av_hwframe_transfer_data(va, sw_in, 0), "upload to vaapi") < 0)
        return 2;

    for (int route = 0; route < 2; route++) {
        AVFrame *vk = av_frame_alloc(), *drm = av_frame_alloc(), *out = av_frame_alloc();
        const char *name = route ? "B vaapi->drm_prime->vulkan" : "A vaapi->vulkan (hwmap)";
        int ret;
        vk->hw_frames_ctx = av_buffer_ref(vk_frames);
        vk->format = AV_PIX_FMT_VULKAN;
        if (!route) {
            ret = av_hwframe_map(vk, va, AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT);
        } else {
            drm->format = AV_PIX_FMT_DRM_PRIME;
            ret = av_hwframe_map(drm, va, AV_HWFRAME_MAP_READ);
            if (ret >= 0)
                ret = av_hwframe_map(vk, drm, AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT);
        }
        if (ret < 0) {
            char buf[128];
            av_strerror(ret, buf, sizeof(buf));
            printf("%s %s: map failed: %s (%d)\n", av_get_pix_fmt_name(sw), name, buf, ret);
        } else {
            out->format = sw;
            ret = av_hwframe_transfer_data(out, vk, 0);
            if (ret < 0) {
                check(ret, "download from vulkan");
                failed = 1;
            } else {
                long bad = compare(sw_in, out, p010);
                printf("%s %s: mapped, %ld byte(s) differ\n", av_get_pix_fmt_name(sw), name, bad);
                if (bad) failed = 1;
            }
        }
        av_frame_free(&out);
        av_frame_free(&vk);
        av_frame_free(&drm);
    }
    av_frame_free(&va);
    av_frame_free(&sw_in);
    av_buffer_unref(&vk_frames);
    av_buffer_unref(&va_frames);
    av_buffer_unref(&vk_dev);
    av_buffer_unref(&va_dev);
    return failed;
}
