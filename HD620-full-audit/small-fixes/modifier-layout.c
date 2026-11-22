/* vkCreateImage with an explicit DRM format modifier layout (TODO item 26).
 *
 * Y-tiled R8G8B8A8 256x64 with rowPitch 1000 (not a multiple of the 128-byte
 * tile width) must fail with VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT.
 * rowPitch 1024 must succeed.  Exit 0 when both behave, 1 otherwise.
 */
#include <stdio.h>
#include <vulkan/vulkan.h>

#define I915_FORMAT_MOD_Y_TILED 0x0100000000000002ull

static VkResult create(VkDevice dev, VkDeviceSize pitch)
{
    VkSubresourceLayout layout = { .offset = 0, .rowPitch = pitch };
    VkImageDrmFormatModifierExplicitCreateInfoEXT mod = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
        .drmFormatModifier = I915_FORMAT_MOD_Y_TILED, .drmFormatModifierPlaneCount = 1,
        .pPlaneLayouts = &layout };
    VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext = &mod,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = { 256, 64, 1 },
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, .usage = VK_IMAGE_USAGE_SAMPLED_BIT };
    VkImage img = VK_NULL_HANDLE;
    VkResult r = vkCreateImage(dev, &ici, NULL, &img);
    if (img) vkDestroyImage(dev, img, NULL);
    return r;
}

int main(void)
{
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    if (vkCreateInstance(&ii, NULL, &inst)) return 2;
    uint32_t n = 1;
    VkPhysicalDevice phys;
    vkEnumeratePhysicalDevices(inst, &n, &phys);
    const char *ext[] = { VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME };
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qi, .enabledExtensionCount = 1, .ppEnabledExtensionNames = ext };
    VkDevice dev;
    if (vkCreateDevice(phys, &di, NULL, &dev)) return 2;
    VkResult bad = create(dev, 1000), good = create(dev, 1024);
    printf("rowPitch 1000: %d%s\n", bad, bad == VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT ?
           " (VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT)" :
           bad == VK_ERROR_OUT_OF_DEVICE_MEMORY ? " (VK_ERROR_OUT_OF_DEVICE_MEMORY)" : "");
    printf("rowPitch 1024: %d%s\n", good, good == VK_SUCCESS ? " (VK_SUCCESS)" : "");
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);
    return bad == VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT && good == VK_SUCCESS ? 0 : 1;
}
