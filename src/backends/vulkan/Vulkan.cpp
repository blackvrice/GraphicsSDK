#include <graphics/Graphics.hpp>
#include "../../foundation/DeviceGeneration.hpp"
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include "VulkanValidation.hpp"
#include "Spirv.hpp"
#include "Timestamp.hpp"
#include "Creation.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <list>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#ifdef _WIN32
#include <Windows.h>
#include <vulkan/vulkan_win32.h>
#else
#include <dlfcn.h>
#ifdef GFX_HAS_VULKAN_XCB
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
#include <cstdlib>
#endif
#endif

namespace gfx {
namespace {
auto fail(ErrorCode code, const char* message, VkResult native = VK_SUCCESS) {
    return std::unexpected(Error{code, message, native});
}
auto nativeError(VkResult result, const char* operation) {
    auto code = result == VK_ERROR_DEVICE_LOST ? ErrorCode::DeviceLost :
        result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY ? ErrorCode::OutOfMemory :
        result == VK_TIMEOUT ? ErrorCode::Timeout : ErrorCode::BackendFailure;
    return fail(code, operation, result);
}
#include "Xcb.hpp"
bool markerText(std::string_view text) {
    if (text.size() > UINT32_MAX) return false;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i++]);
        if (!lead) return false; // A native C string cannot preserve embedded NUL.
        if (lead < 0x80) continue;
        unsigned tail{}, value{}, minimum{};
        if (lead >= 0xc2 && lead <= 0xdf) { tail = 1; value = lead & 0x1f; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { tail = 2; value = lead & 0x0f; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { tail = 3; value = lead & 7; minimum = 0x10000; }
        else return false;
        if (text.size() - i < tail) return false;
        while (tail--) {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xc0) != 0x80) return false;
            value = (value << 6) | (byte & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

struct Loader {
#ifdef _WIN32
    HMODULE module{LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    PFN_vkGetInstanceProcAddr get{module ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(module, "vkGetInstanceProcAddr")) : nullptr};
    ~Loader() { if (module) FreeLibrary(module); }
#else
    void* module{dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL)};
    PFN_vkGetInstanceProcAddr get{module ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(module, "vkGetInstanceProcAddr")) : nullptr};
    ~Loader() { if (module) dlclose(module); }
#endif
};
#define INSTANCE_FUNCTIONS(X) \
    X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties2) \
    X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceImageFormatProperties) X(GetPhysicalDeviceFormatProperties) X(GetPhysicalDeviceFeatures) X(GetPhysicalDeviceFeatures2) X(CreateDevice) X(GetDeviceProcAddr) X(EnumerateDeviceExtensionProperties)
struct Instance {
    std::shared_ptr<Loader> loader;
    VkInstance handle{};
    VkDebugUtilsMessengerEXT messenger{};
    PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger{};
    vulkan::ValidationCounter validation;
    bool presentation{};
    bool swapchainColorSpace{};
    bool debugUtils{};
    PFN_vkDestroySurfaceKHR DestroySurfaceKHR{};
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR{};
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR{};
    PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR GetPhysicalDeviceSurfaceCapabilities2KHR{};
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR{};
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR{};
#ifdef _WIN32
    PFN_vkCreateWin32SurfaceKHR CreateWin32SurfaceKHR{};
#elif defined(GFX_HAS_VULKAN_XCB)
    PFN_vkCreateXcbSurfaceKHR CreateXcbSurfaceKHR{};
    PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR GetPhysicalDeviceXcbPresentationSupportKHR{};
#endif
#define DECLARE(name) PFN_vk##name name{};
    INSTANCE_FUNCTIONS(DECLARE)
#undef DECLARE
    ~Instance() {
        if (messenger) destroyMessenger(handle, messenger, nullptr);
        if (handle && DestroyInstance) DestroyInstance(handle, nullptr);
    }
};
Result<std::shared_ptr<Instance>> createInstance(bool validation) {
    auto instance = std::make_shared<Instance>();
    instance->loader = std::make_shared<Loader>();
    const auto get = instance->loader->get;
    if (!get) return fail(ErrorCode::Unsupported, "Vulkan loader unavailable");
    const auto create = reinterpret_cast<PFN_vkCreateInstance>(get(nullptr, "vkCreateInstance"));
    const auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(get(nullptr, "vkEnumerateInstanceVersion"));
    if (!create || !enumerateVersion) return fail(ErrorCode::Unsupported, "Vulkan 1.1 loader required");
    std::uint32_t api{};
    if (auto r = enumerateVersion(&api); r != VK_SUCCESS) return nativeError(r, "vkEnumerateInstanceVersion");
    if (api < VK_API_VERSION_1_1) return fail(ErrorCode::Unsupported, "Vulkan 1.1 required");
    const char* layer = vulkan::ValidationConfiguration::layer;
    std::vector<const char*> enabledExtensions;
    const auto enumerateExtensions = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(get(nullptr, "vkEnumerateInstanceExtensionProperties"));
    if (enumerateExtensions) {
        std::uint32_t count{};
        auto r = enumerateExtensions(nullptr, &count, nullptr);
        if (r != VK_SUCCESS) return nativeError(r, "Enumerate instance extensions");
        std::vector<VkExtensionProperties> names(count);
        r = enumerateExtensions(nullptr, &count, names.data());
        if (r != VK_SUCCESS) return nativeError(r, "Enumerate instance extensions");
        names.resize(count);
        const auto has = [&](const char* name) { return std::any_of(names.begin(), names.end(), [&](auto& p) { return std::strcmp(p.extensionName, name) == 0; }); };
        instance->debugUtils = has(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#ifdef _WIN32
        instance->presentation = has(VK_KHR_SURFACE_EXTENSION_NAME) && has(VK_KHR_WIN32_SURFACE_EXTENSION_NAME) &&
            has(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) && has(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        if (instance->presentation) enabledExtensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
            VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME};
#elif defined(GFX_HAS_VULKAN_XCB)
        instance->presentation = has(VK_KHR_SURFACE_EXTENSION_NAME) && has(VK_KHR_XCB_SURFACE_EXTENSION_NAME) &&
            has(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) && has(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        if (instance->presentation) enabledExtensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME,
            VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME};
#endif
        if (instance->presentation && has(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME)) {
            instance->swapchainColorSpace = true;
            enabledExtensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
        }
    }
    if (instance->debugUtils) enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (validation) {
        const auto layers = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(get(nullptr, "vkEnumerateInstanceLayerProperties"));
        const auto extensions = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(get(nullptr, "vkEnumerateInstanceExtensionProperties"));
        if (!layers || !extensions) return fail(ErrorCode::Unsupported, "Vulkan validation enumeration unavailable");
        std::uint32_t count{};
        auto r = layers(&count, nullptr);
        if (r != VK_SUCCESS) return nativeError(r, "Enumerate validation layers");
        std::vector<VkLayerProperties> available(count);
        r = layers(&count, available.data());
        if (r != VK_SUCCESS) return nativeError(r, "Enumerate validation layers");
        available.resize(count);
        if (std::none_of(available.begin(), available.end(), [&](auto& p) { return std::strcmp(p.layerName, layer) == 0; }))
            return fail(ErrorCode::Unsupported, "VK_LAYER_KHRONOS_validation is not installed");
        std::vector<VkExtensionProperties> names;
        for (auto provider : {static_cast<const char*>(nullptr), layer}) {
            r = extensions(provider, &count, nullptr);
            if (r != VK_SUCCESS) return nativeError(r, "Enumerate validation extensions");
            std::vector<VkExtensionProperties> provided(count);
            r = extensions(provider, &count, provided.data());
            if (r != VK_SUCCESS) return nativeError(r, "Enumerate validation extensions");
            provided.resize(count); names.insert(names.end(), provided.begin(), provided.end());
        }
        for (auto required : vulkan::ValidationConfiguration::extensions)
            if (std::none_of(names.begin(), names.end(), [&](auto& p) { return std::strcmp(p.extensionName, required) == 0; }))
                return fail(ErrorCode::Unsupported, "Vulkan validation requires debug_utils, validation_features and layer_settings");
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "GraphicsSDK"; app.apiVersion = VK_API_VERSION_1_1;
    vulkan::ValidationConfiguration validationConfig(instance->validation);
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    if (validation) {
        info.enabledLayerCount = 1; info.ppEnabledLayerNames = &layer;
        for (const auto required : vulkan::ValidationConfiguration::extensions)
            if (std::none_of(enabledExtensions.begin(), enabledExtensions.end(), [&](const auto name) { return std::strcmp(name, required) == 0; }))
                enabledExtensions.push_back(required);
        instance->debugUtils = true;
        info.pNext = &validationConfig.layerSettings;
    }
    info.enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size()); info.ppEnabledExtensionNames = enabledExtensions.data();
    if (auto r = vulkan::acquireOutput(instance->handle, create, &info, nullptr); r != VK_SUCCESS) return nativeError(r, "vkCreateInstance");
#define LOAD(name) instance->name = reinterpret_cast<PFN_vk##name>(get(instance->handle, "vk" #name)); \
    if (!instance->name) return fail(ErrorCode::Unsupported, "Vulkan instance entry point missing: vk" #name);
    INSTANCE_FUNCTIONS(LOAD)
#undef LOAD
    if (instance->presentation) {
#ifdef GFX_HAS_VULKAN_XCB
#define LOAD_WSI(name) instance->name = reinterpret_cast<PFN_vk##name>(get(instance->handle, "vk" #name)); if (!instance->name) instance->presentation = false;
#else
#define LOAD_WSI(name) instance->name = reinterpret_cast<PFN_vk##name>(get(instance->handle, "vk" #name)); if (!instance->name) return fail(ErrorCode::Unsupported, "Vulkan surface entry point missing: vk" #name);
#endif
        LOAD_WSI(DestroySurfaceKHR) LOAD_WSI(GetPhysicalDeviceSurfaceSupportKHR) LOAD_WSI(GetPhysicalDeviceSurfaceCapabilitiesKHR)
        LOAD_WSI(GetPhysicalDeviceSurfaceFormatsKHR) LOAD_WSI(GetPhysicalDeviceSurfacePresentModesKHR) LOAD_WSI(GetPhysicalDeviceSurfaceCapabilities2KHR)
#ifdef _WIN32
        LOAD_WSI(CreateWin32SurfaceKHR)
#elif defined(GFX_HAS_VULKAN_XCB)
        LOAD_WSI(CreateXcbSurfaceKHR) LOAD_WSI(GetPhysicalDeviceXcbPresentationSupportKHR)
#endif
#undef LOAD_WSI
    }
    if (validation) {
        const auto make = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(get(instance->handle, "vkCreateDebugUtilsMessengerEXT"));
        instance->destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(get(instance->handle, "vkDestroyDebugUtilsMessengerEXT"));
        if (!make || !instance->destroyMessenger) return fail(ErrorCode::Unsupported, "Vulkan debug messenger unavailable");
        if (auto r = vulkan::acquireOutput(instance->messenger, make, instance->handle, &validationConfig.messenger, nullptr); r != VK_SUCCESS) return nativeError(r, "Create validation messenger");
    }
    return instance;
}
bool validQueueType(QueueType t) { return t == QueueType::Graphics || t == QueueType::Compute || t == QueueType::Copy; }
std::size_t queueIndex(QueueType t) { return static_cast<std::size_t>(t); }
VkPipelineStageFlags stageScope(QueueType t, bool includeHost = false) {
    const VkPipelineStageFlags host = includeHost ? VK_PIPELINE_STAGE_HOST_BIT : 0u;
    if (t == QueueType::Copy) return VK_PIPELINE_STAGE_TRANSFER_BIT | host;
    if (t == QueueType::Compute) return VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | host;
    return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | host;
}
struct Candidate {
    AdapterInfo info;
    VkPhysicalDevice physical{};
    VkPhysicalDeviceMemoryProperties memory{};
    std::uint32_t family{};
    std::array<std::uint32_t, 3> families{};
    std::array<std::uint32_t, 3> timestampBits{};
    bool dedicatedComputeQueue{};
    bool dedicatedCopyQueue{};
    std::uint32_t maxColorAttachments{};
    std::uint32_t maxFramebufferWidth{}, maxFramebufferHeight{};
    VkPhysicalDeviceLimits limits{};
    VkPhysicalDeviceFeatures features{};
};
Result<std::vector<Candidate>> candidates(const std::shared_ptr<Instance>& instance) {
    std::uint32_t count{};
    auto r = instance->EnumeratePhysicalDevices(instance->handle, &count, nullptr);
    if (r != VK_SUCCESS) return nativeError(r, "Enumerate Vulkan physical devices");
    std::vector<VkPhysicalDevice> physical(count);
    r = instance->EnumeratePhysicalDevices(instance->handle, &count, physical.data());
    if (r != VK_SUCCESS) return nativeError(r, "Enumerate Vulkan physical devices");
    physical.resize(count);
    std::vector<Candidate> result;
    for (auto device : physical) {
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; properties.pNext = &id;
        instance->GetPhysicalDeviceProperties2(device, &properties);
        if (properties.properties.apiVersion < VK_API_VERSION_1_1) continue;
        std::uint32_t families{};
        instance->GetPhysicalDeviceQueueFamilyProperties(device, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        instance->GetPhysicalDeviceQueueFamilyProperties(device, &families, queues.data()); queues.resize(families);
        auto graphicsIt = std::find_if(queues.begin(), queues.end(), [](auto& q) { return q.queueCount && (q.queueFlags & VK_QUEUE_GRAPHICS_BIT); });
        if (graphicsIt == queues.end()) continue;
        const auto graphicsFamily = static_cast<std::uint32_t>(graphicsIt - queues.begin());
        std::uint32_t computeFamily = graphicsFamily;
        for (std::uint32_t i = 0; i < queues.size(); ++i) {
            if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && !(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                computeFamily = i;
                break;
            }
        }
        std::uint32_t copyFamily = graphicsFamily;
        for (std::uint32_t i = 0; i < queues.size(); ++i) {
            if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                !(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && !(queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                copyFamily = i;
                break;
            }
        }
        if (copyFamily == graphicsFamily) {
            for (std::uint32_t i = 0; i < queues.size(); ++i) {
                if (i != computeFamily && queues[i].queueCount &&
                    (queues[i].queueFlags & VK_QUEUE_TRANSFER_BIT) && !(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    copyFamily = i;
                    break;
                }
            }
        }
        Candidate c; c.physical = device; c.family = graphicsFamily;
        c.families = {graphicsFamily, computeFamily, copyFamily};
        c.timestampBits = {
            queues[graphicsFamily].timestampValidBits,
            queues[computeFamily].timestampValidBits,
            queues[copyFamily].timestampValidBits
        };
        c.dedicatedComputeQueue = computeFamily != graphicsFamily;
        c.dedicatedCopyQueue = copyFamily != graphicsFamily;
        c.maxColorAttachments = properties.properties.limits.maxColorAttachments;
        c.maxFramebufferWidth = properties.properties.limits.maxFramebufferWidth;
        c.maxFramebufferHeight = properties.properties.limits.maxFramebufferHeight;
        c.limits = properties.properties.limits; instance->GetPhysicalDeviceFeatures(device, &c.features);
        c.info.id = {Backend::Vulkan, 14695981039346656037ull};
        bool nonzero{};
        for (auto byte : id.deviceUUID) { nonzero |= byte != 0; c.info.id.value = (c.info.id.value ^ byte) * 1099511628211ull; }
        if (!nonzero || !c.info.id.value || std::any_of(result.begin(), result.end(), [&](auto& p) { return p.info.id == c.info.id; }))
            return fail(ErrorCode::Unsupported, "Ambiguous Vulkan device UUID identity");
        c.info.name = properties.properties.deviceName;
        c.info.vendorId = properties.properties.vendorID; c.info.deviceId = properties.properties.deviceID;
        c.info.softwareAdapter = properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        instance->GetPhysicalDeviceMemoryProperties(device, &c.memory);
        for (std::uint32_t i = 0; i < c.memory.memoryHeapCount; ++i) {
            const auto& heap = c.memory.memoryHeaps[i];
            if (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) c.info.dedicatedVideoMemory += heap.size;
            else c.info.sharedSystemMemory += heap.size;
        }
        result.push_back(std::move(c));
    }
    return result;
}
#define DEVICE_FUNCTIONS(X) \
    X(DestroyDevice) X(GetDeviceQueue) X(DeviceWaitIdle) X(QueueWaitIdle) X(QueueSubmit) \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) \
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(GetImageSparseMemoryRequirements) X(BindImageMemory) X(CmdCopyBufferToImage) X(CmdCopyImageToBuffer) X(CmdResolveImage) \
    X(CreateImageView) X(DestroyImageView) X(CreateRenderPass) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) X(CmdBeginRenderPass) X(CmdEndRenderPass) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateGraphicsPipelines) X(CreateComputePipelines) X(DestroyPipeline) \
    X(CreateSampler) X(DestroySampler) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CmdBindDescriptorSets) \
    X(CreateQueryPool) X(DestroyQueryPool) X(CmdResetQueryPool) X(CmdWriteTimestamp) X(GetQueryPoolResults) \
    X(CmdBindPipeline) X(CmdSetViewport) X(CmdSetScissor) X(CmdDraw) X(CmdBindVertexBuffers) X(CmdBindIndexBuffer) X(CmdDrawIndexed) X(CmdDrawIndirect) X(CmdDrawIndexedIndirect) X(CmdDispatch) X(CmdDispatchIndirect) \
    X(MapMemory) X(UnmapMemory) X(FlushMappedMemoryRanges) X(InvalidateMappedMemoryRanges) \
    X(CreateCommandPool) X(DestroyCommandPool) X(ResetCommandPool) X(AllocateCommandBuffers) X(FreeCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(CmdCopyBuffer) X(CmdPipelineBarrier) X(CreateFence) X(DestroyFence) X(GetFenceStatus) X(WaitForFences) X(CreateSemaphore) X(DestroySemaphore)
struct NativeDevice {
    std::shared_ptr<Instance> instance;
    VkDevice handle{};
    VkQueue queue{};
    std::array<VkQueue, 3> queues{};
    bool presentation{};
    bool hdrMetadata{};
    bool incrementalPresent{};
    bool timelineSemaphores{};
    PFN_vkCmdInsertDebugUtilsLabelEXT CmdInsertDebugUtilsLabelEXT{};
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR{};
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR{};
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR{};
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR{};
    PFN_vkQueuePresentKHR QueuePresentKHR{};
    PFN_vkSetHdrMetadataEXT SetHdrMetadataEXT{};
    PFN_vkGetSemaphoreCounterValueKHR GetSemaphoreCounterValueKHR{};
    PFN_vkWaitSemaphoresKHR WaitSemaphoresKHR{};
    PFN_vkSignalSemaphoreKHR SignalSemaphoreKHR{};
#define DECLARE(name) PFN_vk##name name{};
    DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
    void release() { if (handle) { DestroyDevice(handle, nullptr); handle = {}; } }
    ~NativeDevice() { if (handle && DestroyDevice) release(); }
};
struct NativeSurface {
    std::shared_ptr<NativeDevice> native;
    VkSurfaceKHR handle{};
    void* window{};
    bool lost{}, expired{};
#ifdef GFX_HAS_VULKAN_XCB
    void* connection{}; std::uint32_t windowId{};
#endif
    ~NativeSurface() { if (handle) native->instance->DestroySurfaceKHR(native->instance->handle, handle, nullptr); }
};
bool surfaceWindowReady(NativeSurface& surface) {
#ifdef GFX_HAS_VULKAN_XCB
    if (!surface.expired && !xcbWindowAlive(surface.connection, surface.windowId)) surface.expired = true;
    return !surface.expired;
#elif defined(_WIN32)
    return IsWindow(static_cast<HWND>(surface.window)) != FALSE;
#else
    (void)surface; return false;
#endif
}
struct NativeSwapchain {
    std::shared_ptr<NativeSurface> surface;
    VkSwapchainKHR handle{};
    std::vector<VkSemaphore> finished;
    std::uint32_t current{};
    bool alive{true}, acquired{}, invalid{};
    void release() {
        auto& n = *surface->native;
        if (handle) n.DestroySwapchainKHR(n.handle, handle, nullptr);
        for (auto s : finished) if (s) n.DestroySemaphore(n.handle, s, nullptr);
        handle = {}; finished.clear(); alive = false; acquired = false;
    }
    ~NativeSwapchain() { release(); }
};
struct Allocation;
struct NativeMemoryHeap {
    struct PlacedEntry {
        std::weak_ptr<Allocation> allocation;
        std::uint64_t offset{};
        std::uint64_t size{};
    };
    std::shared_ptr<NativeDevice> native;
    VkDeviceMemory memory{};
    MemoryHeapDesc desc{};
    std::uint32_t memoryTypeIndex{};
    mutable std::vector<PlacedEntry> entries;
    void release() {
        if (memory) native->FreeMemory(native->handle, memory, nullptr);
        memory = {};
    }
    ~NativeMemoryHeap() { release(); }
};
struct Allocation {
    std::shared_ptr<NativeDevice> native;
    std::shared_ptr<NativeMemoryHeap> heap;
    VkBuffer buffer{};
    VkImage image{};
    VkImageView view{};
    VkImageView attachmentView{};
    VkDeviceMemory memory{};
    VkDeviceSize bytes{};
    std::shared_ptr<NativeSwapchain> swapchain;
    std::uint32_t imageIndex{};
    bool coherent{};
    BufferDesc desc;
    TextureDesc textureDesc;
    TextureFootprint footprint;
    bool imageInitialized{};
    bool contentsInitialized{};
    ResourceState state{ResourceState::Common};
    std::uint64_t version{};
    std::array<std::uint64_t, 3> lastUse{};
    void release() {
        if (buffer) native->DestroyBuffer(native->handle, buffer, nullptr);
        if (attachmentView && attachmentView != view) native->DestroyImageView(native->handle, attachmentView, nullptr);
        if (view) native->DestroyImageView(native->handle, view, nullptr);
        if (image && !swapchain) native->DestroyImage(native->handle, image, nullptr);
        if (memory && !heap) native->FreeMemory(native->handle, memory, nullptr);
        buffer = {}; view = {}; attachmentView = {}; image = {}; memory = {};
        heap.reset();
    }
    ~Allocation() { release(); }
};
bool isSampledTextureBinding(BindingType type) {
    return type == BindingType::Texture2D || type == BindingType::Texture1D ||
           type == BindingType::Texture3D || type == BindingType::Texture2DArray ||
           type == BindingType::TextureCube;
}
BindingType bindingTypeForDimension(TextureDimension dim) {
    switch (dim) {
    case TextureDimension::Texture1D: return BindingType::Texture1D;
    case TextureDimension::Texture3D: return BindingType::Texture3D;
    case TextureDimension::Texture2DArray: return BindingType::Texture2DArray;
    case TextureDimension::TextureCube: return BindingType::TextureCube;
    case TextureDimension::Texture2D: default: return BindingType::Texture2D;
    }
}
VkImageType vulkanImageType(TextureDimension dim) {
    switch (dim) {
    case TextureDimension::Texture1D: return VK_IMAGE_TYPE_1D;
    case TextureDimension::Texture3D: return VK_IMAGE_TYPE_3D;
    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    default: return VK_IMAGE_TYPE_2D;
    }
}
VkImageViewType vulkanImageViewType(TextureDimension dim) {
    switch (dim) {
    case TextureDimension::Texture1D: return VK_IMAGE_VIEW_TYPE_1D;
    case TextureDimension::Texture3D: return VK_IMAGE_VIEW_TYPE_3D;
    case TextureDimension::Texture2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureDimension::TextureCube: return VK_IMAGE_VIEW_TYPE_CUBE;
    case TextureDimension::Texture2D: default: return VK_IMAGE_VIEW_TYPE_2D;
    }
}
bool currentBackbuffer(const Allocation& a) {
    if (!a.swapchain) return true;
    const auto& s = *a.swapchain;
    return s.alive && !s.invalid && s.acquired && s.current == a.imageIndex &&
        !s.surface->lost && surfaceWindowReady(*s.surface);
}
struct Use { std::shared_ptr<Allocation> allocation; std::uint64_t version; ResourceState shadow; bool imageInitialized, contentsInitialized, mutated{}, aliasedPending{}; };
struct NativePass {
    std::shared_ptr<NativeDevice> native;
    VkRenderPass pass{};
    VkFramebuffer framebuffer{};
    ~NativePass() {
        if (framebuffer) native->DestroyFramebuffer(native->handle, framebuffer, nullptr);
        if (pass) native->DestroyRenderPass(native->handle, pass, nullptr);
    }
};
struct NativePipeline {
    std::shared_ptr<NativeDevice> native;
    VkPipeline pipeline{};
    VkPipelineLayout layout{};
    VkRenderPass pass{};
    std::vector<Format> colors;
    std::optional<Format> depth;
    std::uint32_t sampleCount{1};
    DepthState depthState;
    struct Input { std::uint32_t slot{}, stride{}, extent{}, alignment{1}; VertexInputRate rate{}; };
    std::vector<Input> inputs;
    BindingLayout bindings;
    std::vector<unsigned> minimumBytes;
    std::vector<VkSampler> samplers;
    VkDescriptorSetLayout setLayout{};
    unsigned descriptorSize{};
    bool sampled{}, linear{}, isCompute{};
    void release() {
        if (pipeline) native->DestroyPipeline(native->handle, pipeline, nullptr);
        if (layout) native->DestroyPipelineLayout(native->handle, layout, nullptr);
        if (setLayout) native->DestroyDescriptorSetLayout(native->handle, setLayout, nullptr);
        for (auto s : samplers) native->DestroySampler(native->handle, s, nullptr);
        if (pass) native->DestroyRenderPass(native->handle, pass, nullptr);
        pipeline = {}; layout = {}; pass = {}; setLayout = {}; samplers.clear();
    }
    ~NativePipeline() { release(); }
};
struct VertexView { std::shared_ptr<Allocation> allocation; std::uint64_t offset{}, bytes{}; std::uint32_t stride{}; };
struct IndexView { std::shared_ptr<Allocation> allocation; std::uint64_t bytes{}; std::uint32_t element{}; };
struct NativeTextureView {
    std::shared_ptr<NativeDevice> native;
    std::shared_ptr<Allocation> allocation;
    VkImageView view{};
    TextureViewDesc desc;
    void release() {
        if (view) native->DestroyImageView(native->handle, view, nullptr);
        view = {};
        allocation.reset();
    }
    ~NativeTextureView() { release(); }
};
struct NativeBindings {
    std::shared_ptr<NativeDevice> native;
    std::shared_ptr<NativePipeline> pipeline;
    std::vector<std::shared_ptr<Allocation>> resources;
    std::vector<ResourceState> expectedStates;
    std::vector<std::shared_ptr<NativeTextureView>> textureViews;
    VkDescriptorPool pool{};
    VkDescriptorSet set{};
    ~NativeBindings() { if (pool) native->DestroyDescriptorPool(native->handle, pool, nullptr); }
};
struct NativeQueryPool {
    std::shared_ptr<NativeDevice> native;
    VkQueryPool handle{};
    void release() {
        if (handle) native->DestroyQueryPool(native->handle, handle, nullptr);
        handle = {};
    }
    ~NativeQueryPool() { release(); }
};
struct NativeTimelineSemaphore {
    std::shared_ptr<NativeDevice> native;
    VkSemaphore handle{};
    std::uint64_t lastSignaled{};
    void release() {
        if (handle) native->DestroySemaphore(native->handle, handle, nullptr);
        handle = {};
    }
    ~NativeTimelineSemaphore() { release(); }
};
struct VulkanCommands;
struct NativeCommandPool {
    std::mutex mutex;
    std::shared_ptr<NativeDevice> native;
    VkCommandPool handle{};
    QueueType qType{QueueType::Graphics};
    std::atomic<std::thread::id> recordingThread{};
    std::uint64_t epoch{1};
    std::uint64_t lastSubmitted{};
    std::uint32_t openCount{};
    bool ownerAlive{true};
    std::vector<VkCommandBuffer> retiredBuffers;
    std::vector<VulkanCommands*> liveLists;
    void release() {
        std::scoped_lock lock(mutex);
        retiredBuffers.clear();
        if (handle) native->DestroyCommandPool(native->handle, handle, nullptr);
        handle = {};
    }
    ~NativeCommandPool() { release(); }
};
struct Recording {
    std::shared_ptr<NativeDevice> native;
    std::shared_ptr<NativeCommandPool> sharedPool;
    std::shared_ptr<NativeQueryPool> sharedQueries;
    QueueType qType{QueueType::Graphics};
    VkCommandPool pool{};
    VkCommandBuffer command{};
    bool open{}, ended{}, inPass{};
    std::uint64_t submitted{};
    std::vector<Use> uses;
    std::vector<std::unique_ptr<NativePass>> passes;
    std::vector<std::shared_ptr<NativeTextureView>> textureViews;
    std::vector<std::shared_ptr<NativePipeline>> pipelines;
    std::shared_ptr<NativePipeline> selectedPipeline;
    std::shared_ptr<NativePipeline> selectedComputePipeline;
    std::vector<Format> passColors;
    bool passDepth{}, depthReadOnly{};
    std::uint32_t width{}, height{}, passSampleCount{1};
    std::array<VertexView, 32> vertices;
    IndexView indices;
    std::vector<std::unique_ptr<NativeBindings>> snapshots;
    NativeBindings* selectedBindings{};
    NativeBindings* selectedComputeBindings{};
    unsigned descriptors{};
    VkQueryPool queries{};
    std::array<bool, 2> timestampWritten{};
    bool timestampsResolved{}, timestampStartFirst{};
    std::chrono::steady_clock::time_point submittedAt;
    std::optional<double> timestampResult;
    struct RecordedScope {
        std::string name;
        std::uint32_t depth{};
        std::uint32_t beginSlot{};
        std::uint32_t endSlot{};
        bool closed{};
    };
    std::vector<RecordedScope> scopes;
    std::vector<std::uint32_t> openScopeStack;
    std::uint32_t nextScopeSlot{};
    bool scopesResolved{};
    std::optional<std::vector<TimestampScopeResult>> cachedScopeResults;
    VkQueryPool queryHandle() const { return sharedQueries ? sharedQueries->handle : queries; }
    void release() {
        if (pool) native->DestroyCommandPool(native->handle, pool, nullptr);
        if (queries) native->DestroyQueryPool(native->handle, queries, nullptr);
        queries = {}; timestampWritten = {}; timestampsResolved = timestampStartFirst = false; timestampResult.reset();
        scopes.clear(); openScopeStack.clear(); nextScopeSlot = 0; scopesResolved = false; cachedScopeResults.reset();
        pool = {}; command = {}; sharedQueries.reset(); sharedPool.reset();
        selectedBindings = {}; selectedComputeBindings = {}; snapshots.clear(); descriptors = 0; selectedPipeline.reset(); selectedComputePipeline.reset(); pipelines.clear(); vertices = {}; indices = {}; passColors.clear(); passes.clear(); textureViews.clear(); uses.clear(); open = ended = inPass = false;
    }
    ~Recording() { release(); }
};
struct Pending {
    VkFence fence{};
    std::uint64_t serial{};
    std::vector<std::shared_ptr<Recording>> recordings;
    std::vector<std::shared_ptr<NativeTimelineSemaphore>> timelineSemaphores;
    std::vector<std::pair<std::shared_ptr<NativeTimelineSemaphore>, std::uint64_t>> signaledTimelines;
};
struct QueueSlot {
    std::uint32_t family{};
    std::uint32_t timestampBits{};
    std::uint64_t id{};
    std::uint64_t last{};
    std::atomic<std::uint64_t> completed{0};
    std::list<Pending> pending;
};
struct State {
    std::shared_ptr<NativeDevice> native;
    VkPhysicalDevice physical{};
    VkPhysicalDeviceMemoryProperties memory{};
    std::uint32_t family{};
    std::vector<std::uint32_t> uniqueFamilies;
    std::array<QueueSlot, 3> queues;
    std::uint32_t timestampBits{};
    std::uint32_t maxColorAttachments{};
    std::uint32_t maxFramebufferWidth{}, maxFramebufferHeight{};
    VkPhysicalDeviceLimits limits{};
    bool fullDrawIndexUint32{};
    bool multiDrawIndirect{};
    std::uint32_t maxDrawIndirectCount{1};
    bool textureCompressionBC{};
    bool textureCompressionETC2{};
    bool textureCompressionASTC_LDR{};
    bool compressionFeatureEnabled(Format f) const noexcept {
        if (f >= Format::BC1UNorm && f <= Format::BC7UNorm) return textureCompressionBC;
        if (f >= Format::ETC2RGB8UNorm && f <= Format::EACRG11UNorm) return textureCompressionETC2;
        if (f >= Format::ASTC4x4UNorm && f <= Format::ASTC8x8UNorm) return textureCompressionASTC_LDR;
        return true;
    }
    std::thread::id thread{std::this_thread::get_id()};
    std::uint64_t id{detail::allocateDeviceGeneration()};
    std::shared_ptr<detail::ProfilingTracker> profiler;
    std::atomic<bool> retired{false}, lost{false};
    std::mutex recordingsMutex;
    std::vector<std::weak_ptr<Allocation>> allocations;
    std::vector<std::weak_ptr<NativeMemoryHeap>> memoryHeaps;
    std::vector<std::weak_ptr<NativeTextureView>> imageViews;
    std::vector<std::weak_ptr<Recording>> recordings;
    std::vector<std::weak_ptr<NativeCommandPool>> commandPools;
    std::vector<std::weak_ptr<NativeQueryPool>> queryPools;
    std::vector<std::weak_ptr<NativePipeline>> pipelines;
    std::vector<std::weak_ptr<NativeSwapchain>> swapchains;
    std::vector<std::weak_ptr<NativeTimelineSemaphore>> timelineSemaphores;
    std::vector<std::weak_ptr<int>> objects;
    Result<void> checkAliveForWorker() const {
        if (retired.load(std::memory_order_acquire) || lost.load(std::memory_order_acquire))
            return fail(ErrorCode::DeviceLost, "Vulkan device generation retired or lost", VK_ERROR_DEVICE_LOST);
        return {};
    }
    Result<void> check() const {
        if (std::this_thread::get_id() != thread) return fail(ErrorCode::InvalidState, "Vulkan operations require the creating thread");
        return checkAliveForWorker();
    }
    Result<void> accept(VkResult r, const char* operation) {
        if (r == VK_ERROR_DEVICE_LOST) lost.store(true, std::memory_order_release);
        if (r != VK_SUCCESS) return nativeError(r, operation);
        return {};
    }
    Result<void> pollQueue(QueueType qType) {
        if (auto ok = check(); !ok) return ok;
        auto& q = queues[queueIndex(qType)];
        while (!q.pending.empty()) {
            auto& first = q.pending.front();
            const auto r = native->GetFenceStatus(native->handle, first.fence);
            if (r == VK_NOT_READY) break;
            if (auto ok = accept(r, "vkGetFenceStatus"); !ok) return ok;
            q.completed.store(first.serial, std::memory_order_release);
            native->DestroyFence(native->handle, first.fence, nullptr); q.pending.pop_front();
        }
        return {};
    }
    Result<void> poll() {
        for (QueueType t : {QueueType::Graphics, QueueType::Compute, QueueType::Copy}) {
            if (auto ok = pollQueue(t); !ok) return ok;
        }
        return {};
    }
    void retire() {
        if (retired.load(std::memory_order_acquire) || !native) return;
        // The device is the lifetime boundary; public children remain inert descriptions.
        native->DeviceWaitIdle(native->handle);
        for (auto& q : queues) {
            for (auto& p : q.pending) native->DestroyFence(native->handle, p.fence, nullptr);
            q.pending.clear();
        }
        {
            std::scoped_lock lock(recordingsMutex);
            for (auto& weak : recordings) if (auto value = weak.lock()) value->release();
            recordings.clear();
        }
        for (auto& weak : commandPools) if (auto value = weak.lock()) value->release();
        for (auto& weak : queryPools) if (auto value = weak.lock()) value->release();
        for (auto& weak : pipelines) if (auto value = weak.lock()) value->release();
        for (auto& weak : imageViews) if (auto value = weak.lock()) value->release();
        for (auto& weak : allocations) if (auto value = weak.lock()) value->release();
        for (auto& weak : memoryHeaps) if (auto value = weak.lock()) value->release();
        for (auto& weak : swapchains) if (auto value = weak.lock()) value->release();
        for (auto& weak : timelineSemaphores) if (auto value = weak.lock()) value->release();
        swapchains.clear();
        timelineSemaphores.clear();
        commandPools.clear(); queryPools.clear(); pipelines.clear(); imageViews.clear(); allocations.clear(); memoryHeaps.clear(); objects.clear();
        native->release(); retired.store(true, std::memory_order_release);
    }
    ~State() { retire(); }
};
struct VulkanTimelineFence final : TimelineFence {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativeTimelineSemaphore> sem;
    std::shared_ptr<int> token;
    Result<std::uint64_t> completedValue() override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        std::uint64_t value{};
        if (auto ok = owner->accept(owner->native->GetSemaphoreCounterValueKHR(owner->native->handle, sem->handle, &value), "vkGetSemaphoreCounterValueKHR"); !ok) {
            return std::unexpected(ok.error());
        }
        return value;
    }
    Result<void> signal(std::uint64_t value) override {
        auto current = completedValue(); if (!current) return std::unexpected(current.error());
        if (value <= *current || value <= sem->lastSignaled || value == UINT64_MAX) {
            return fail(ErrorCode::InvalidArgument, "TimelineFence signal value must strictly advance");
        }
        VkSemaphoreSignalInfoKHR info{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR};
        info.semaphore = sem->handle;
        info.value = value;
        if (auto ok = owner->accept(owner->native->SignalSemaphoreKHR(owner->native->handle, &info), "vkSignalSemaphoreKHR"); !ok) return ok;
        sem->lastSignaled = value;
        return {};
    }
    Result<void> wait(std::uint64_t value, std::chrono::milliseconds timeout) override {
        if (auto ok = owner->check(); !ok) return ok;
        if (timeout.count() < 0 || timeout.count() > 4294967294ll) {
            return fail(ErrorCode::InvalidArgument, "TimelineFence timeout outside finite range");
        }
        VkSemaphoreWaitInfoKHR info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR};
        info.semaphoreCount = 1;
        info.pSemaphores = &sem->handle;
        info.pValues = &value;
        const bool track = owner->profiler && owner->profiler->isEnabled();
        const auto t0 = track ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        const auto r = owner->native->WaitSemaphoresKHR(
            owner->native->handle, &info, static_cast<std::uint64_t>(timeout.count()) * 1000000ull);
        if (r == VK_TIMEOUT) return fail(ErrorCode::Timeout, "TimelineFence wait timed out", r);
        if (auto ok = owner->accept(r, "vkWaitSemaphoresKHR"); !ok) return ok;
        // Retire any internal submission fences whose timeline signal has now completed
        // so readback/reset/statistics immediately observe completion without a driver race.
        for (auto& q : owner->queues) {
            for (auto& p : q.pending) {
                bool signaled = false;
                for (const auto& [sigSem, sigVal] : p.signaledTimelines) {
                    if (sigSem == sem && sigVal <= value) { signaled = true; break; }
                }
                if (signaled) {
                    if (auto ok = owner->accept(owner->native->WaitForFences(owner->native->handle, 1, &p.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences after timeline wait"); !ok) return ok;
                }
            }
        }
        auto polled = owner->poll();
        if (polled && track) {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
            owner->profiler->recordTimelineWait(ns > 0 ? static_cast<std::uint64_t>(ns) : 0u);
        }
        return polled;
    }
};
struct VulkanMemoryHeap final : MemoryHeap {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativeMemoryHeap> heap;
    std::shared_ptr<int> token;
    MemoryHeapDesc description() const override { return heap->desc; }
    Result<HeapFragmentationStats> fragmentationStats() const override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        std::erase_if(heap->entries, [](const NativeMemoryHeap::PlacedEntry& e) { return e.allocation.expired(); });
        std::vector<HeapAllocationRange> ranges;
        ranges.reserve(heap->entries.size());
        for (const auto& e : heap->entries) ranges.push_back({e.offset, e.size});
        return computeHeapFragmentationStats(heap->desc.size, ranges, true);
    }
    Result<void> setResident(bool) override {
        if (auto ok = owner->check(); !ok) return ok;
        return fail(ErrorCode::Unsupported, "Vulkan backend does not provide explicit per-heap residency eviction");
    }
    Result<bool> isResident() const override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return true;
    }
};
struct VulkanBuffer final : Buffer {
    std::shared_ptr<State> owner;
    std::shared_ptr<Allocation> allocation;
    std::shared_ptr<int> token;
    BufferDesc description() const override { return allocation->desc; }
    Result<void> write(std::uint64_t offset, std::span<const std::byte> data) override {
        if (auto ok = owner->poll(); !ok) return ok;
        auto& a = *allocation;
        if (a.desc.memory != Memory::Upload || offset > a.desc.size || data.size() > a.desc.size - offset)
            return fail(ErrorCode::InvalidArgument, "Upload write memory or range invalid");
        for (std::size_t qi = 0; qi < 3; ++qi) {
            if (a.lastUse[qi] > owner->queues[qi].completed.load(std::memory_order_acquire))
                return fail(ErrorCode::InvalidState, "Upload buffer busy");
        }
        if (a.version == UINT64_MAX) return fail(ErrorCode::InvalidState, "Upload buffer version exhausted");
        if (!data.empty()) {
            void* mapped{};
            if (auto ok = owner->accept(owner->native->MapMemory(owner->native->handle, a.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory upload"); !ok) return ok;
            std::memcpy(static_cast<std::byte*>(mapped) + offset, data.data(), data.size());
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE}; range.memory = a.memory; range.size = VK_WHOLE_SIZE;
            auto r = a.coherent ? VK_SUCCESS : owner->native->FlushMappedMemoryRanges(owner->native->handle, 1, &range);
            owner->native->UnmapMemory(owner->native->handle, a.memory);
            // A failed flush still invalidates older observations of the written bytes.
            ++a.version;
            return owner->accept(r, "vkFlushMappedMemoryRanges");
        }
        ++a.version; return {};
    }
    Result<std::vector<std::byte>> read(std::uint64_t offset, std::uint64_t bytes) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        auto& a = *allocation;
        if (a.desc.memory != Memory::Readback || offset > a.desc.size || bytes > a.desc.size - offset || bytes > SIZE_MAX)
            return fail(ErrorCode::InvalidArgument, "Readback read memory or range invalid");
        for (std::size_t qi = 0; qi < 3; ++qi) {
            if (a.lastUse[qi] > owner->queues[qi].completed.load(std::memory_order_acquire))
                return fail(ErrorCode::InvalidState, "Readback buffer busy");
        }
        std::vector<std::byte> result(static_cast<std::size_t>(bytes));
        if (bytes) {
            void* mapped{};
            if (auto ok = owner->accept(owner->native->MapMemory(owner->native->handle, a.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory readback"); !ok) return std::unexpected(ok.error());
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE}; range.memory = a.memory; range.size = VK_WHOLE_SIZE;
            auto r = a.coherent ? VK_SUCCESS : owner->native->InvalidateMappedMemoryRanges(owner->native->handle, 1, &range);
            if (r == VK_SUCCESS) std::memcpy(result.data(), static_cast<std::byte*>(mapped) + offset, result.size());
            owner->native->UnmapMemory(owner->native->handle, a.memory);
            if (auto ok = owner->accept(r, "vkInvalidateMappedMemoryRanges"); !ok) return std::unexpected(ok.error());
        }
        return result;
    }
};
struct VulkanTexture final : Texture {
    std::shared_ptr<State> owner;
    std::shared_ptr<Allocation> allocation;
    std::shared_ptr<int> token;
    TextureDesc description() const override { return allocation->textureDesc; }
    TextureFootprint footprint() const override { return allocation->footprint; }
};
struct VulkanTextureView final : TextureView {
    std::shared_ptr<State> owner;
    std::shared_ptr<VulkanTexture> parent;
    std::shared_ptr<NativeTextureView> native;
    std::shared_ptr<int> token;
    TextureViewDesc description() const override { return native->desc; }
    TexturePtr texture() const override { return parent; }
};
struct VulkanPipeline final : Pipeline {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativePipeline> native;
    std::shared_ptr<int> token;
};
VkSampleCountFlagBits vulkanSampleCount(std::uint32_t count) {
    switch (count) {
    case 2u: return VK_SAMPLE_COUNT_2_BIT;
    case 4u: return VK_SAMPLE_COUNT_4_BIT;
    case 8u: return VK_SAMPLE_COUNT_8_BIT;
    default: return VK_SAMPLE_COUNT_1_BIT;
    }
}
VkImageLayout imageLayout(ResourceState state) {
    if (state == ResourceState::Present) return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    if (state == ResourceState::CopySource || state == ResourceState::ResolveSource) return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    if (state == ResourceState::CopyDestination || state == ResourceState::ResolveDestination) return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if (state == ResourceState::RenderTarget) return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if (state == ResourceState::ShaderRead) return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (state == ResourceState::DepthWrite) return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if (state == ResourceState::DepthRead) return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    if (state == ResourceState::UnorderedAccess) return VK_IMAGE_LAYOUT_GENERAL;
    return VK_IMAGE_LAYOUT_GENERAL;
}
VkAccessFlags access(ResourceState state) {
    if (state == ResourceState::Present) return 0;
    if (state == ResourceState::CopySource || state == ResourceState::ResolveSource) return VK_ACCESS_TRANSFER_READ_BIT;
    if (state == ResourceState::CopyDestination || state == ResourceState::ResolveDestination) return VK_ACCESS_TRANSFER_WRITE_BIT;
    if (state == ResourceState::RenderTarget) return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (state == ResourceState::ShaderRead) return VK_ACCESS_SHADER_READ_BIT;
    if (state == ResourceState::ConstantBuffer) return VK_ACCESS_UNIFORM_READ_BIT;
    if (state == ResourceState::DepthWrite) return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (state == ResourceState::DepthRead) return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    if (state == ResourceState::VertexBuffer) return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    if (state == ResourceState::IndexBuffer) return VK_ACCESS_INDEX_READ_BIT;
    if (state == ResourceState::VertexAndIndexBuffer) return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
    if (state == ResourceState::UnorderedAccess) return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    if (state == ResourceState::IndirectArgument) return VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
}
VkFormat textureFormat(Format f) {
    switch (f) {
    case Format::RGBA8UNorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::BGRA8UNorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::D32Float: return VK_FORMAT_D32_SFLOAT;
    case Format::BC1UNorm: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case Format::BC2UNorm: return VK_FORMAT_BC2_UNORM_BLOCK;
    case Format::BC3UNorm: return VK_FORMAT_BC3_UNORM_BLOCK;
    case Format::BC4UNorm: return VK_FORMAT_BC4_UNORM_BLOCK;
    case Format::BC5UNorm: return VK_FORMAT_BC5_UNORM_BLOCK;
    case Format::BC6HUFloat: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
    case Format::BC7UNorm: return VK_FORMAT_BC7_UNORM_BLOCK;
    case Format::ETC2RGB8UNorm: return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
    case Format::ETC2RGBA8UNorm: return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
    case Format::EACR11UNorm: return VK_FORMAT_EAC_R11_UNORM_BLOCK;
    case Format::EACRG11UNorm: return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
    case Format::ASTC4x4UNorm: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
    case Format::ASTC6x6UNorm: return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
    case Format::ASTC8x8UNorm: return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
    case Format::RGBA8SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::BGRA8SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::RGB10A2UNorm: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    }
    return VK_FORMAT_UNDEFINED;
}
unsigned texelBytes(Format f) { return f == Format::RGBA16Float ? 8u : 4u; }
VkImageAspectFlags imageAspect(const Allocation& a) { return a.textureDesc.depthStencil ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT; }
std::array<VkSubpassDependency, 2> passDependencies() {
    // ALL_COMMANDS in a subpass dependency covers graphics, so include TRANSFER explicitly.
    constexpr auto attachmentStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    constexpr auto attachmentAccess = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    return {{{VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, attachmentStages,
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, attachmentAccess, 0},
        {0, VK_SUBPASS_EXTERNAL, attachmentStages, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        attachmentAccess, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, 0}}};
}
bool vertexState(ResourceState s) { return s == ResourceState::VertexBuffer || s == ResourceState::VertexAndIndexBuffer; }
bool indexState(ResourceState s) { return s == ResourceState::IndexBuffer || s == ResourceState::VertexAndIndexBuffer; }
VkFormat vertexFormat(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float: return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::UNorm8x4: return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}
std::uint32_t vertexBytes(VertexFormat format) {
    switch (format) { case VertexFormat::Float2: return 8; case VertexFormat::Float3: return 12; case VertexFormat::Float4: return 16; default: return 4; }
}
struct VulkanCommands final : CommandList {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativeCommandPool> commandPool;
    QueueType qType{QueueType::Graphics};
    VkCommandBuffer pooledCommand{};
    std::shared_ptr<NativeQueryPool> pooledQueries;
    std::uint64_t recordedEpoch{};
    std::shared_ptr<Recording> record;
    std::shared_ptr<int> token;
    QueueType queueType() const override { return qType; }
    ~VulkanCommands() override {
        if (!commandPool) return;
        std::scoped_lock lock(commandPool->mutex);
        std::erase(commandPool->liveLists, this);
        if (record && record->open) {
            if (commandPool->openCount > 0) --commandPool->openCount;
            record->open = false;
        }
        if (pooledCommand && commandPool->handle) {
            commandPool->retiredBuffers.push_back(pooledCommand);
        }
        pooledCommand = {};
    }
    Result<void> ready() const {
        if (commandPool) {
            if (commandPool->recordingThread.load(std::memory_order_acquire) != std::this_thread::get_id())
                return fail(ErrorCode::InvalidState, "Vulkan command recording must run on the pool's bound recording thread");
            if (auto ok = owner->checkAliveForWorker(); !ok) return ok;
            if (!commandPool->ownerAlive) return fail(ErrorCode::InvalidState, "Owning CommandPool has been destroyed");
        } else {
            if (auto ok = owner->check(); !ok) return ok;
        }
        if (!record || !record->open) return fail(ErrorCode::InvalidState, "Command list is not recording");
        return {};
    }
    Result<void> outsidePass() const {
        if (auto ok = ready(); !ok) return ok;
        if (record->inPass) return fail(ErrorCode::InvalidState, "Operation requires no active render pass");
        return {};
    }
    Result<void> unsupported() const {
        if (auto ok = ready(); !ok) return ok;
        return fail(ErrorCode::Unsupported, "Vulkan operation is outside the initial shader-drawing path");
    }
    Result<std::shared_ptr<Allocation>> buffer(const BufferPtr& buffer) const {
        auto value = std::dynamic_pointer_cast<VulkanBuffer>(buffer);
        if (!value) return fail(buffer ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument, "Expected a Vulkan buffer");
        if (value->owner != owner) return fail(ErrorCode::WrongDevice, "Buffer belongs to another device generation");
        return value->allocation;
    }
    Result<std::shared_ptr<Allocation>> texture(const TexturePtr& texture) const {
        auto value = std::dynamic_pointer_cast<VulkanTexture>(texture);
        if (!value) return fail(texture ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument, "Expected a Vulkan texture");
        if (value->owner != owner) return fail(ErrorCode::WrongDevice, "Texture belongs to another device generation");
        if (!currentBackbuffer(*value->allocation)) return fail(ErrorCode::InvalidState, "Backbuffer is not current or its swapchain/window expired");
        return value->allocation;
    }
    Result<std::shared_ptr<NativeTextureView>> textureView(const TextureViewPtr& view) const {
        if (!view) return fail(ErrorCode::InvalidArgument, "Null Vulkan texture view");
        auto value = std::dynamic_pointer_cast<VulkanTextureView>(view);
        if (!value || value->owner != owner) return fail(ErrorCode::WrongDevice, "Texture view belongs to another device generation");
        if (!value->native || !value->native->allocation || !currentBackbuffer(*value->native->allocation))
            return fail(ErrorCode::InvalidState, "Backbuffer is not current or its swapchain/window expired");
        return value->native;
    }
    Use* find(const std::shared_ptr<Allocation>& a) {
        const auto it = std::find_if(record->uses.begin(), record->uses.end(), [&](auto& use) { return use.allocation == a; });
        return it == record->uses.end() ? nullptr : &*it;
    }
    ResourceState shadow(const std::shared_ptr<Allocation>& a) { auto use = find(a); return use ? use->shadow : a->state; }
    Use& retain(const std::shared_ptr<Allocation>& a, bool markMutated = false) {
        if (auto use = find(a)) {
            if (markMutated) use->mutated = true;
            return *use;
        }
        return record->uses.emplace_back(Use{a, a->version, a->state, a->imageInitialized, a->contentsInitialized, markMutated});
    }
    Result<void> begin() override {
        auto& qSlot = owner->queues[queueIndex(qType)];
        if (commandPool) {
            std::scoped_lock lock(commandPool->mutex);
            if (auto ok = owner->checkAliveForWorker(); !ok) return ok;
            if (!commandPool->ownerAlive) return fail(ErrorCode::InvalidState, "Owning CommandPool has been destroyed");
            if (commandPool->recordingThread.load(std::memory_order_acquire) != std::this_thread::get_id())
                return fail(ErrorCode::InvalidState, "Vulkan command recording must run on the pool's bound recording thread");
            if (record && record->open) return fail(ErrorCode::InvalidState, "Command list is already recording");
            if (commandPool->openCount > 0) return fail(ErrorCode::InvalidState, "Another command list from this CommandPool is currently recording");
            if (recordedEpoch == commandPool->epoch) return fail(ErrorCode::InvalidState, "Pooled command list requires CommandPool::reset() before re-recording");
            if (record && record->submitted > qSlot.completed.load(std::memory_order_acquire)) return fail(ErrorCode::InvalidState, "Command list is still in flight");
            auto next = std::make_shared<Recording>();
            next->native = owner->native;
            next->sharedPool = commandPool;
            next->qType = qType;
            next->command = pooledCommand;
            next->sharedQueries = pooledQueries;
            VkCommandBufferBeginInfo start{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            start.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (auto ok = owner->accept(owner->native->BeginCommandBuffer(next->command, &start), "vkBeginCommandBuffer"); !ok) return ok;
            if (next->sharedQueries && next->sharedQueries->handle) {
                owner->native->CmdResetQueryPool(next->command, next->sharedQueries->handle, 0, 2u + kMaxTimestampScopesPerCommandList * 2u);
            }
            next->open = true;
            recordedEpoch = commandPool->epoch;
            ++commandPool->openCount;
            {
                std::scoped_lock recLock(owner->recordingsMutex);
                std::erase_if(owner->recordings, [](auto& w) { return w.expired(); });
                owner->recordings.push_back(next);
            }
            record = std::move(next);
            return {};
        }
        if (auto ok = owner->pollQueue(qType); !ok) return ok;
        if (record && (record->open || record->submitted > qSlot.completed.load(std::memory_order_acquire))) return fail(ErrorCode::InvalidState, "Command list open or in flight");
        auto next = std::make_shared<Recording>(); next->native = owner->native; next->qType = qType;
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex = qSlot.family;
        if (auto ok = owner->accept(vulkan::acquireOutput(next->pool, owner->native->CreateCommandPool, owner->native->handle, &pool, nullptr), "vkCreateCommandPool"); !ok) return ok;
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = next->pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
        if (auto ok = owner->accept(vulkan::acquireOutput(next->command, owner->native->AllocateCommandBuffers, owner->native->handle, &allocate), "vkAllocateCommandBuffers"); !ok) return ok;
        VkCommandBufferBeginInfo start{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; start.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (auto ok = owner->accept(owner->native->BeginCommandBuffer(next->command, &start), "vkBeginCommandBuffer"); !ok) return ok;
        if (qType != QueueType::Copy && vulkan::timestampSupported(qSlot.timestampBits, owner->limits.timestampPeriod)) {
            constexpr std::uint32_t kTotalQueries = 2u + kMaxTimestampScopesPerCommandList * 2u;
            VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; queries.queryType = VK_QUERY_TYPE_TIMESTAMP; queries.queryCount = kTotalQueries;
            if (auto ok = owner->accept(vulkan::acquireOutput(next->queries, owner->native->CreateQueryPool, owner->native->handle, &queries, nullptr), "vkCreateQueryPool"); !ok) return ok;
            owner->native->CmdResetQueryPool(next->command, next->queries, 0, kTotalQueries);
        }
        next->open = true;
        {
            std::scoped_lock recLock(owner->recordingsMutex);
            std::erase_if(owner->recordings, [](auto& w) { return w.expired(); });
            owner->recordings.push_back(next);
        }
        record = std::move(next); return {};
    }
    Result<void> end() override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (!record->openScopeStack.empty()) return fail(ErrorCode::InvalidState, "Unclosed timestamp scope before CommandList::end()");
        const auto r = owner->native->EndCommandBuffer(record->command);
        record->open = false; record->ended = r == VK_SUCCESS;
        if (commandPool) {
            std::scoped_lock lock(commandPool->mutex);
            if (commandPool->openCount > 0) --commandPool->openCount;
        }
        return owner->accept(r, "vkEndCommandBuffer");
    }
    Result<void> transition(const BufferPtr& value, ResourceState target) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy && target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination)
            return fail(ErrorCode::InvalidArgument, "Copy command list only supports Common, CopySource, and CopyDestination buffer states");
        if (qType == QueueType::Compute && (vertexState(target) || indexState(target)))
            return fail(ErrorCode::InvalidArgument, "Compute command list does not support vertex/index buffer states");
        const auto a = buffer(value); if (!a) return std::unexpected(a.error());
        if (target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination && !vertexState(target) && !indexState(target) && target != ResourceState::ConstantBuffer && target != ResourceState::UnorderedAccess && target != ResourceState::IndirectArgument)
            return fail(ErrorCode::Unsupported, "Vulkan transfer state unsupported");
        if (target == ResourceState::UnorderedAccess && !(*a)->desc.storage)
            return fail(ErrorCode::InvalidArgument, "UnorderedAccess state requires a storage buffer");
        if (((*a)->desc.memory == Memory::Upload && target != ResourceState::CopySource && !vertexState(target) && !indexState(target) && target != ResourceState::ConstantBuffer && target != ResourceState::IndirectArgument) ||
            ((*a)->desc.memory == Memory::Readback && target != ResourceState::CopyDestination))
            return fail(ErrorCode::InvalidArgument, "Host buffer state is fixed");
        const auto before = shadow(*a);
        if (qType == QueueType::Copy && (*a)->desc.memory == Memory::Device &&
            before != ResourceState::Common && before != ResourceState::CopySource && before != ResourceState::CopyDestination)
            return fail(ErrorCode::InvalidState, "Copy command list cannot transition buffer from a non-copy state");
        if (qType == QueueType::Compute && (vertexState(before) || indexState(before)))
            return fail(ErrorCode::InvalidState, "Compute command list cannot transition buffer from a vertex/index state");
        record->uses.reserve(record->uses.size() + 1);
        auto& use = retain(*a);
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask = access(use.shadow) | ((*a)->desc.memory == Memory::Upload ? VK_ACCESS_HOST_WRITE_BIT : 0);
        barrier.dstAccessMask = access(target) | ((*a)->desc.memory == Memory::Readback ? VK_ACCESS_HOST_READ_BIT : 0);
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = (*a)->buffer; barrier.size = VK_WHOLE_SIZE;
        const VkPipelineStageFlags scope = stageScope(qType, true);
        owner->native->CmdPipelineBarrier(record->command, scope, scope, 0, 0, nullptr, 1, &barrier, 0, nullptr);
        if (use.shadow != target) use.mutated = true;
        use.shadow = target;
        if (owner->profiler) owner->profiler->recordBarrier();
        return {};
    }
    Result<void> copyBuffer(const BufferPtr& source, const BufferPtr& destination, std::uint64_t bytes) override {
        return copyBufferRegion(source, 0, destination, 0, bytes);
    }
    Result<void> copyBufferRegion(const BufferPtr& source, std::uint64_t sourceOffset, const BufferPtr& destination, std::uint64_t destinationOffset, std::uint64_t bytes) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = buffer(source); if (!src) return std::unexpected(src.error());
        auto dst = buffer(destination); if (!dst) return std::unexpected(dst.error());
        if (!bytes ||
            sourceOffset > (*src)->desc.size || bytes > (*src)->desc.size - sourceOffset ||
            destinationOffset > (*dst)->desc.size || bytes > (*dst)->desc.size - destinationOffset ||
            *src == *dst ||
            (*src)->desc.memory == Memory::Readback || (*dst)->desc.memory == Memory::Upload)
            return fail(ErrorCode::InvalidArgument, "Buffer copy range or alias invalid");
        if (shadow(*src) != ResourceState::CopySource || shadow(*dst) != ResourceState::CopyDestination)
            return fail(ErrorCode::InvalidState, "Buffer copy requires explicit source/destination states");
        // Finish all validation and allocations before retaining either argument.
        record->uses.reserve(record->uses.size() + 2); retain(*src); retain(*dst, true);
        VkBufferMemoryBarrier before[2]{};
        for (auto& b : before) { b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.size = VK_WHOLE_SIZE; }
        before[0].buffer = (*src)->buffer; before[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT; before[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        before[1].buffer = (*dst)->buffer; before[1].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_READ_BIT; before[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        owner->native->CmdPipelineBarrier(record->command, stageScope(qType, true), VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 2, before, 0, nullptr);
        VkBufferCopy region{sourceOffset, destinationOffset, bytes};
        owner->native->CmdCopyBuffer(record->command, (*src)->buffer, (*dst)->buffer, 1, &region);
        if ((*dst)->desc.memory == Memory::Readback) {
            auto after = before[1]; after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; after.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            owner->native->CmdPipelineBarrier(record->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &after, 0, nullptr);
        }
        if (owner->profiler) owner->profiler->recordCopy();
        return {};
    }
    Result<void> transition(const TexturePtr& value, ResourceState target) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy && target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination)
            return fail(ErrorCode::InvalidArgument, "Copy command list only supports Common, CopySource, and CopyDestination texture states");
        if (qType == QueueType::Compute && (target == ResourceState::RenderTarget || target == ResourceState::DepthWrite || target == ResourceState::DepthRead || target == ResourceState::Present || target == ResourceState::ResolveSource || target == ResourceState::ResolveDestination))
            return fail(ErrorCode::InvalidArgument, "Compute command list does not support graphics/present/resolve texture states");
        auto a = texture(value); if (!a) return std::unexpected(a.error());
        const bool multisampled = (*a)->textureDesc.sampleCount > 1u;
        if (target == ResourceState::IndirectArgument)
            return fail(ErrorCode::InvalidArgument, "IndirectArgument state is only valid for buffers");
        if (target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination && target != ResourceState::RenderTarget && target != ResourceState::ShaderRead && target != ResourceState::DepthWrite && target != ResourceState::DepthRead && target != ResourceState::Present && target != ResourceState::UnorderedAccess && target != ResourceState::ResolveSource && target != ResourceState::ResolveDestination)
            return fail(ErrorCode::Unsupported, "Vulkan texture transfer state unsupported");
        if (target == ResourceState::Present && !(*a)->swapchain) return fail(ErrorCode::InvalidArgument, "Present requires a swapchain backbuffer");
        if (target == ResourceState::UnorderedAccess && !(*a)->textureDesc.storage) return fail(ErrorCode::InvalidArgument, "UnorderedAccess state requires a storage texture");
        if ((target == ResourceState::ResolveSource || target == ResourceState::ResolveDestination) && ((*a)->textureDesc.depthStencil || (*a)->swapchain))
            return fail(ErrorCode::Unsupported, "Depth or swapchain texture resolve transition is unsupported");
        if (target == ResourceState::ResolveDestination && multisampled)
            return fail(ErrorCode::InvalidArgument, "Multisampled texture cannot be a resolve destination");
        if ((target == ResourceState::CopySource || target == ResourceState::CopyDestination) && multisampled)
            return fail(ErrorCode::InvalidArgument, "Multisampled texture does not support direct copy states; use ResolveSource");
        if (target == ResourceState::ShaderRead && multisampled) return fail(ErrorCode::Unsupported, "Multisampled texture sampling unsupported");
        if (target == ResourceState::ShaderRead && (*a)->swapchain) return fail(ErrorCode::Unsupported, "Backbuffer sampling unsupported");
        if ((target == ResourceState::DepthWrite || target == ResourceState::DepthRead) && !(*a)->textureDesc.depthStencil)
            return fail(ErrorCode::InvalidArgument, "Depth state requires depth texture");
        if (target == ResourceState::ShaderRead && (*a)->textureDesc.depthStencil) return fail(ErrorCode::Unsupported, "Depth sampling unsupported");
        if (target == ResourceState::RenderTarget && !(*a)->textureDesc.renderTarget)
            return fail(ErrorCode::InvalidArgument, "Texture was not created as a render target");
        const auto before = shadow(*a);
        if (qType == QueueType::Copy &&
            before != ResourceState::Common && before != ResourceState::CopySource && before != ResourceState::CopyDestination)
            return fail(ErrorCode::InvalidState, "Copy command list cannot transition texture from a non-copy state");
        if (qType == QueueType::Compute &&
            (before == ResourceState::RenderTarget || before == ResourceState::DepthWrite || before == ResourceState::DepthRead ||
             before == ResourceState::Present || before == ResourceState::ResolveSource || before == ResourceState::ResolveDestination))
            return fail(ErrorCode::InvalidState, "Compute command list cannot transition texture from a graphics/present/resolve state");
        record->uses.reserve(record->uses.size() + 1);
        auto& use = retain(*a);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = use.imageInitialized ? imageLayout(use.shadow) : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = imageLayout(target);
        barrier.srcAccessMask = use.imageInitialized ? access(use.shadow) : (use.aliasedPending ? VK_ACCESS_MEMORY_WRITE_BIT : 0u);
        barrier.dstAccessMask = access(target);
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = (*a)->image;
        barrier.subresourceRange = {imageAspect(**a), 0, std::max(1u, (*a)->textureDesc.mipLevels), 0, std::max(1u, (*a)->textureDesc.arrayLayers)};
        const VkPipelineStageFlags srcStage = (use.imageInitialized || use.aliasedPending) ? stageScope(qType, false) : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        const VkPipelineStageFlags dstStage = stageScope(qType, false);
        owner->native->CmdPipelineBarrier(record->command, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        if (!use.imageInitialized || use.shadow != target) use.mutated = true;
        use.shadow = target; use.imageInitialized = true; use.aliasedPending = false;
        if (owner->profiler) owner->profiler->recordBarrier();
        return {};
    }
    Result<void> aliasingBarrier(const BufferPtr& before, const BufferPtr& after) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (!after) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires a non-null destination buffer");
        auto dst = buffer(after); if (!dst) return std::unexpected(dst.error());
        if (!(*dst)->heap) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires placed resources");
        std::shared_ptr<Allocation> src;
        if (before) {
            auto checked = buffer(before); if (!checked) return std::unexpected(checked.error());
            src = std::move(*checked);
            if (!src->heap) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires placed resources");
            if (src == *dst) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires distinct before and after resources");
        }
        record->uses.reserve(record->uses.size() + 2);
        if (src) retain(src);
        retain(*dst, true);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        const VkPipelineStageFlags scope = stageScope(qType, false);
        owner->native->CmdPipelineBarrier(record->command, scope, scope, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        if (owner->profiler) owner->profiler->recordBarrier();
        return {};
    }
    Result<void> aliasingBarrier(const TexturePtr& before, const TexturePtr& after) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (!after) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires a non-null destination texture");
        auto dst = texture(after); if (!dst) return std::unexpected(dst.error());
        if (!(*dst)->heap) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires placed resources");
        std::shared_ptr<Allocation> src;
        if (before) {
            auto checked = texture(before); if (!checked) return std::unexpected(checked.error());
            src = std::move(*checked);
            if (!src->heap) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires placed resources");
            if (src == *dst) return fail(ErrorCode::InvalidArgument, "Aliasing barrier requires distinct before and after resources");
        }
        record->uses.reserve(record->uses.size() + 2);
        if (src) retain(src);
        auto& dstUse = retain(*dst, true);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        const VkPipelineStageFlags scope = stageScope(qType, false);
        owner->native->CmdPipelineBarrier(record->command, scope, scope, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        // After aliasing, the destination image layout is undefined and must transition from UNDEFINED.
        dstUse.imageInitialized = false;
        dstUse.contentsInitialized = false;
        dstUse.shadow = ResourceState::Common;
        dstUse.aliasedPending = true;
        dstUse.mutated = true;
        if (owner->profiler) owner->profiler->recordBarrier();
        return {};
    }
    Result<void> copyTexture(const std::shared_ptr<Allocation>& image, const std::shared_ptr<Allocation>& buffer, bool upload) {
        if (image->textureDesc.sampleCount > 1u)
            return fail(ErrorCode::InvalidArgument, "Direct buffer copy is invalid for multisampled textures; resolve first");
        if (buffer->desc.size < image->footprint.size ||
            buffer->desc.memory == (upload ? Memory::Readback : Memory::Upload))
            return fail(ErrorCode::InvalidArgument, "Texture copy buffer size or memory kind invalid");
        if (shadow(image) != (upload ? ResourceState::CopyDestination : ResourceState::CopySource) ||
            shadow(buffer) != (upload ? ResourceState::CopySource : ResourceState::CopyDestination))
            return fail(ErrorCode::InvalidState, "Texture copy requires explicit source/destination states");
        // Validate both arguments before taking references or version snapshots.
        record->uses.reserve(record->uses.size() + 2); retain(image, upload); retain(buffer, !upload);
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
        b.dstAccessMask = upload ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = buffer->buffer; b.size = VK_WHOLE_SIZE;
        VkImageMemoryBarrier i{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        i.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        i.dstAccessMask = upload ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_TRANSFER_READ_BIT;
        i.oldLayout = i.newLayout = imageLayout(shadow(image));
        i.srcQueueFamilyIndex = i.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        i.image = image->image;
        i.subresourceRange = {imageAspect(*image), 0, std::max(1u, image->textureDesc.mipLevels), 0, std::max(1u, image->textureDesc.arrayLayers)};
        owner->native->CmdPipelineBarrier(record->command, stageScope(qType, true),
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &b, 1, &i);
        const auto blockInfo = queryFormatBlockInfo(image->textureDesc.format);
        if (!blockInfo) return std::unexpected(blockInfo.error());
        const std::uint32_t sliceHeight = image->textureDesc.dimension == TextureDimension::Texture1D ? 1u : image->textureDesc.height;
        VkBufferImageCopy region{};
        region.bufferRowLength = (image->footprint.rowPitch / blockInfo->bytesPerBlock) * blockInfo->blockWidth;
        region.bufferImageHeight = image->footprint.height * blockInfo->blockHeight;
        region.imageSubresource = {imageAspect(*image), 0, 0, 1};
        region.imageExtent = {image->textureDesc.width, sliceHeight,
            image->textureDesc.dimension == TextureDimension::Texture3D ? image->textureDesc.depth : 1u};
        if (upload) {
            owner->native->CmdCopyBufferToImage(record->command, buffer->buffer, image->image, i.newLayout, 1, &region);
            find(image)->contentsInitialized = true;
        }
        else {
            owner->native->CmdCopyImageToBuffer(record->command, image->image, i.newLayout, buffer->buffer, 1, &region);
            if (buffer->desc.memory == Memory::Readback) {
                b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                owner->native->CmdPipelineBarrier(record->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                    0, 0, nullptr, 1, &b, 0, nullptr);
            }
        }
        if (owner->profiler) owner->profiler->recordCopy();
        return {};
    }
    Result<void> copyBufferToTexture(const BufferPtr& source, const TexturePtr& destination) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = buffer(source); if (!src) return std::unexpected(src.error());
        auto dst = texture(destination); if (!dst) return std::unexpected(dst.error());
        return copyTexture(*dst, *src, true);
    }
    Result<void> copyTextureToBuffer(const TexturePtr& source, const BufferPtr& destination) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = texture(source); if (!src) return std::unexpected(src.error());
        auto dst = buffer(destination); if (!dst) return std::unexpected(dst.error());
        return copyTexture(*src, *dst, false);
    }
    Result<void> copyTextureRegion(const std::shared_ptr<Allocation>& image, const std::shared_ptr<Allocation>& buffer, const TextureCopyRegion& region, bool upload) {
        const auto& desc = image->textureDesc;
        if (desc.sampleCount > 1u) {
            return fail(ErrorCode::InvalidArgument, "Direct buffer copy is invalid for multisampled textures; resolve first");
        }
        if (region.mipLevel >= desc.mipLevels || region.arrayLayer >= desc.arrayLayers || !region.depth) {
            return fail(ErrorCode::InvalidArgument, "Texture copy region subresource or depth invalid");
        }
        const std::uint32_t mipW = std::max(1u, desc.width >> region.mipLevel);
        const std::uint32_t mipH = desc.dimension == TextureDimension::Texture1D ? 1u : std::max(1u, desc.height >> region.mipLevel);
        const std::uint32_t mipD = desc.dimension == TextureDimension::Texture3D ? std::max(1u, desc.depth >> region.mipLevel) : 1u;
        const auto blockInfo = queryFormatBlockInfo(desc.format);
        if (!blockInfo) return std::unexpected(blockInfo.error());
        auto footprint = computeTextureRegionFootprint(desc.format, region.width, region.height, region.bufferRowPitch);
        if (!footprint) return std::unexpected(footprint.error());
        if (region.x > mipW || region.width > mipW - region.x ||
            region.y > mipH || region.height > mipH - region.y ||
            region.z > mipD || region.depth > mipD - region.z) {
            return fail(ErrorCode::InvalidArgument, "Texture copy region exceeds subresource dimensions");
        }
        if (blockInfo->compressed) {
            if ((region.x % blockInfo->blockWidth) != 0u || (region.y % blockInfo->blockHeight) != 0u) {
                return fail(ErrorCode::InvalidArgument, "Compressed texture copy region origin must be block-aligned");
            }
            const bool alignedW = (region.width % blockInfo->blockWidth) == 0u || (region.x + region.width == mipW);
            const bool alignedH = (region.height % blockInfo->blockHeight) == 0u || (region.y + region.height == mipH);
            if (!alignedW || !alignedH) {
                return fail(ErrorCode::InvalidArgument, "Compressed texture copy region extent must be block-aligned or reach the subresource edge");
            }
        }
        if (footprint->size > UINT64_MAX / region.depth) {
            return fail(ErrorCode::InvalidArgument, "Texture copy region byte size overflow");
        }
        const std::uint64_t totalBytes = footprint->size * region.depth;
        const bool fullSlice = region.x == 0 && region.y == 0 && region.width == mipW && region.height == mipH;
        const bool fullExtent = fullSlice && region.z == 0 && region.depth == mipD;
        if (desc.depthStencil && !fullExtent) {
            return fail(ErrorCode::Unsupported, "Partial sub-region copy is unsupported for depth textures");
        }
        if ((region.bufferOffset % 512u) != 0 ||
            region.bufferOffset > buffer->desc.size || totalBytes > buffer->desc.size - region.bufferOffset ||
            buffer->desc.memory == (upload ? Memory::Readback : Memory::Upload)) {
            return fail(ErrorCode::InvalidArgument, "Texture region copy buffer alignment, bounds or memory kind invalid");
        }
        if (shadow(image) != (upload ? ResourceState::CopyDestination : ResourceState::CopySource) ||
            shadow(buffer) != (upload ? ResourceState::CopySource : ResourceState::CopyDestination)) {
            return fail(ErrorCode::InvalidState, "Texture region copy requires explicit source/destination states");
        }
        record->uses.reserve(record->uses.size() + 2);
        retain(image, upload);
        retain(buffer, !upload);
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
        b.dstAccessMask = upload ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = buffer->buffer; b.size = VK_WHOLE_SIZE;
        VkImageMemoryBarrier i{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        i.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        i.dstAccessMask = upload ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_TRANSFER_READ_BIT;
        i.oldLayout = i.newLayout = imageLayout(shadow(image));
        i.srcQueueFamilyIndex = i.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        i.image = image->image;
        i.subresourceRange = {imageAspect(*image), 0, std::max(1u, desc.mipLevels), 0, std::max(1u, desc.arrayLayers)};
        owner->native->CmdPipelineBarrier(record->command, stageScope(qType, true),
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &b, 1, &i);
        VkBufferImageCopy copy{};
        copy.bufferOffset = region.bufferOffset;
        copy.bufferRowLength = (footprint->rowPitch / blockInfo->bytesPerBlock) * blockInfo->blockWidth;
        copy.bufferImageHeight = footprint->height * blockInfo->blockHeight;
        copy.imageSubresource = {imageAspect(*image), region.mipLevel, region.arrayLayer, 1};
        copy.imageOffset = {static_cast<std::int32_t>(region.x), static_cast<std::int32_t>(region.y), static_cast<std::int32_t>(region.z)};
        copy.imageExtent = {region.width, region.height, region.depth};
        if (upload) {
            owner->native->CmdCopyBufferToImage(record->command, buffer->buffer, image->image, i.newLayout, 1, &copy);
            if (fullSlice) find(image)->contentsInitialized = true;
        } else {
            owner->native->CmdCopyImageToBuffer(record->command, image->image, i.newLayout, buffer->buffer, 1, &copy);
            if (buffer->desc.memory == Memory::Readback) {
                b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                owner->native->CmdPipelineBarrier(record->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                    0, 0, nullptr, 1, &b, 0, nullptr);
            }
        }
        if (owner->profiler) owner->profiler->recordCopy();
        return {};
    }
    Result<void> copyBufferToTextureRegion(const BufferPtr& source, const TexturePtr& destination, const TextureCopyRegion& region) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = buffer(source); if (!src) return std::unexpected(src.error());
        auto dst = texture(destination); if (!dst) return std::unexpected(dst.error());
        return copyTextureRegion(*dst, *src, region, true);
    }
    Result<void> copyTextureToBufferRegion(const TexturePtr& source, const BufferPtr& destination, const TextureCopyRegion& region) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = texture(source); if (!src) return std::unexpected(src.error());
        auto dst = buffer(destination); if (!dst) return std::unexpected(dst.error());
        return copyTextureRegion(*src, *dst, region, false);
    }
    Result<void> resolveTexture(const TexturePtr& source, const TexturePtr& destination) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "resolveTexture requires a Graphics command list");
        auto src = texture(source); if (!src) return std::unexpected(src.error());
        auto dst = texture(destination); if (!dst) return std::unexpected(dst.error());
        if (*src == *dst) return fail(ErrorCode::InvalidArgument, "Resolve source and destination must be distinct textures");
        if ((*src)->textureDesc.depthStencil || (*dst)->textureDesc.depthStencil)
            return fail(ErrorCode::Unsupported, "Depth texture resolve is unsupported");
        if ((*src)->swapchain || (*dst)->swapchain)
            return fail(ErrorCode::Unsupported, "Swapchain backbuffer resolve is unsupported");
        if ((*src)->textureDesc.sampleCount <= 1u || (*dst)->textureDesc.sampleCount != 1u)
            return fail(ErrorCode::InvalidArgument, "resolveTexture requires multisampled source and single-sampled destination");
        if ((*src)->textureDesc.width != (*dst)->textureDesc.width ||
            (*src)->textureDesc.height != (*dst)->textureDesc.height ||
            (*src)->textureDesc.format != (*dst)->textureDesc.format)
            return fail(ErrorCode::InvalidArgument, "Resolve source and destination extent/format must match");
        if (shadow(*src) != ResourceState::ResolveSource || shadow(*dst) != ResourceState::ResolveDestination)
            return fail(ErrorCode::InvalidState, "resolveTexture requires ResolveSource and ResolveDestination states");
        const auto srcUse = find(*src);
        if (!(srcUse ? srcUse->contentsInitialized : (*src)->contentsInitialized))
            return fail(ErrorCode::InvalidState, "Resolve source texture must have initialized contents");
        record->uses.reserve(record->uses.size() + 2);
        retain(*src);
        retain(*dst, true);
        VkImageMemoryBarrier barriers[2]{};
        for (auto& b : barriers) {
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        }
        barriers[0].image = (*src)->image;
        barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, std::max(1u, (*src)->textureDesc.mipLevels), 0, std::max(1u, (*src)->textureDesc.arrayLayers)};
        barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barriers[0].oldLayout = barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[1].image = (*dst)->image;
        barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, std::max(1u, (*dst)->textureDesc.mipLevels), 0, std::max(1u, (*dst)->textureDesc.arrayLayers)};
        barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[1].oldLayout = barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        owner->native->CmdPipelineBarrier(record->command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
        VkImageResolve region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.extent = {(*src)->textureDesc.width, (*src)->textureDesc.height, 1};
        owner->native->CmdResolveImage(record->command, (*src)->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            (*dst)->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        find(*dst)->contentsInitialized = true;
        if (owner->profiler) owner->profiler->recordCopy();
        return {};
    }
    Result<void> beginRenderPass(const TexturePtr& target, Color clear) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "beginRenderPass requires a Graphics command list");
        return beginRenderPass(RenderPassDesc{{{target, LoadOp::Clear, clear}}, {}});
    }
    Result<void> beginRenderPass(const RenderPassDesc& description) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "beginRenderPass requires a Graphics command list");
        if (auto ok = validateRenderPass(description); !ok) return ok;
        if (description.colors.size() > owner->maxColorAttachments)
            return fail(ErrorCode::Unsupported, "Color attachment count exceeds adapter limit");
        std::vector<std::shared_ptr<Allocation>> colors;
        std::vector<std::shared_ptr<NativeTextureView>> retainedViews;
        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> references;
        std::vector<VkImageView> views;
        std::vector<VkClearValue> clears;
        std::vector<Format> formats;
        for (const auto& color : description.colors) {
            std::shared_ptr<Allocation> a;
            VkImageView nativeView{};
            if (color.view) {
                auto v = textureView(color.view); if (!v) return std::unexpected(v.error());
                if ((*v)->desc.usage != TextureViewUsage::RenderTarget)
                    return fail(ErrorCode::InvalidArgument, "Color attachment view requires RenderTarget usage");
                a = (*v)->allocation;
                nativeView = (*v)->view;
                retainedViews.push_back(*v);
            } else {
                auto tex = texture(color.texture); if (!tex) return std::unexpected(tex.error());
                a = *tex;
                nativeView = a->attachmentView ? a->attachmentView : a->view;
            }
            if (shadow(a) != ResourceState::RenderTarget) return fail(ErrorCode::InvalidState, "Attachment requires RenderTarget state");
            const auto use = find(a);
            if (color.load == LoadOp::Load && !(use ? use->contentsInitialized : a->contentsInitialized))
                return fail(ErrorCode::InvalidState, "Load requires initialized attachment contents");
            colors.push_back(a); views.push_back(nativeView);
            formats.push_back(a->textureDesc.format);
            VkAttachmentDescription attachment{};
            attachment.format = textureFormat(a->textureDesc.format);
            attachment.samples = vulkanSampleCount(a->textureDesc.sampleCount);
            attachment.loadOp = color.load == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            references.push_back({static_cast<std::uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
            attachments.push_back(attachment);
            VkClearValue clear{}; clear.color = {{color.clear.r, color.clear.g, color.clear.b, color.clear.a}}; clears.push_back(clear);
        }
        VkAttachmentReference depthReference{};
        const bool hasDepth = description.depth.texture || description.depth.view;
        if (hasDepth) {
            const auto& depth = description.depth;
            std::shared_ptr<Allocation> a;
            VkImageView nativeView{};
            if (depth.view) {
                auto v = textureView(depth.view); if (!v) return std::unexpected(v.error());
                if ((*v)->desc.usage != TextureViewUsage::DepthStencil)
                    return fail(ErrorCode::InvalidArgument, "Depth attachment view requires DepthStencil usage");
                a = (*v)->allocation;
                nativeView = (*v)->view;
                retainedViews.push_back(*v);
            } else {
                auto tex = texture(depth.texture); if (!tex) return std::unexpected(tex.error());
                a = *tex;
                nativeView = a->attachmentView ? a->attachmentView : a->view;
            }
            if (shadow(a) != (depth.readOnly ? ResourceState::DepthRead : ResourceState::DepthWrite)) return fail(ErrorCode::InvalidState, "Depth attachment read/write state mismatch");
            const auto use = find(a);
            if (depth.load == LoadOp::Load && !(use ? use->contentsInitialized : a->contentsInitialized)) return fail(ErrorCode::InvalidState, "Depth Load requires initialized contents");
            colors.push_back(a); views.push_back(nativeView);
            VkAttachmentDescription attachment{}; attachment.format = VK_FORMAT_D32_SFLOAT; attachment.samples = vulkanSampleCount(a->textureDesc.sampleCount);
            attachment.loadOp = depth.load == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = attachment.finalLayout = imageLayout(shadow(a));
            depthReference = {static_cast<unsigned>(attachments.size()), attachment.initialLayout}; attachments.push_back(attachment);
            VkClearValue clear{}; clear.depthStencil = {depth.clear, 0}; clears.push_back(clear);
        }
        // Complete validation and reserve every retention slot before changing recording state.
        record->uses.reserve(record->uses.size() + colors.size()); record->passes.reserve(record->passes.size() + 1);
        record->textureViews.reserve(record->textureViews.size() + retainedViews.size());
        auto pass = std::make_unique<NativePass>(); pass->native = owner->native;
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<std::uint32_t>(references.size()); subpass.pColorAttachments = references.data();
        if (hasDepth) subpass.pDepthStencilAttachment = &depthReference;
        const auto dependencies = passDependencies();
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = static_cast<std::uint32_t>(attachments.size()); info.pAttachments = attachments.data();
        info.subpassCount = 1; info.pSubpasses = &subpass; info.dependencyCount = 2; info.pDependencies = dependencies.data();
        if (auto ok = owner->accept(vulkan::acquireOutput(pass->pass, owner->native->CreateRenderPass, owner->native->handle, &info, nullptr), "vkCreateRenderPass"); !ok) return ok;
        const auto desc = colors.front()->textureDesc;
        const std::uint32_t firstMip = !description.colors.empty()
            ? (description.colors.front().view ? description.colors.front().view->description().baseMipLevel : 0u)
            : (description.depth.view ? description.depth.view->description().baseMipLevel : 0u);
        const std::uint32_t passW = std::max(1u, desc.width >> firstMip);
        const std::uint32_t passH = std::max(1u, desc.height >> firstMip);
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = pass->pass; framebuffer.attachmentCount = static_cast<std::uint32_t>(views.size()); framebuffer.pAttachments = views.data();
        framebuffer.width = passW; framebuffer.height = passH; framebuffer.layers = 1;
        if (auto ok = owner->accept(vulkan::acquireOutput(pass->framebuffer, owner->native->CreateFramebuffer, owner->native->handle, &framebuffer, nullptr), "vkCreateFramebuffer"); !ok) return ok;
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; begin.renderPass = pass->pass; begin.framebuffer = pass->framebuffer;
        begin.renderArea.extent = {passW, passH}; begin.clearValueCount = static_cast<std::uint32_t>(clears.size()); begin.pClearValues = clears.data();
        for (std::size_t idx = 0; idx < colors.size(); ++idx) {
            const bool isDepthAttachment = hasDepth && (idx + 1 == colors.size());
            const bool markMutated = !isDepthAttachment || !description.depth.readOnly;
            retain(colors[idx], markMutated).contentsInitialized = true;
        }
        for (auto& v : retainedViews) record->textureViews.push_back(std::move(v));
        record->passes.push_back(std::move(pass));
        record->passDepth = hasDepth; record->depthReadOnly = description.depth.readOnly;
        record->passColors = std::move(formats); record->width = passW; record->height = passH; record->passSampleCount = desc.sampleCount; record->selectedPipeline.reset(); record->selectedBindings = {};
        owner->native->CmdBeginRenderPass(record->command, &begin, VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{0, float(passH), float(passW), -float(passH), 0, 1};
        const VkRect2D scissor{{0, 0}, {passW, passH}};
        owner->native->CmdSetViewport(record->command, 0, 1, &viewport); owner->native->CmdSetScissor(record->command, 0, 1, &scissor);
        record->inPass = true;
        if (owner->profiler) owner->profiler->recordRenderPass();
        return {};
    }
    Result<void> endRenderPass() override {
        if (auto ok = ready(); !ok) return ok;
        if (!record->inPass) return fail(ErrorCode::InvalidState, "No active render pass");
        owner->native->CmdEndRenderPass(record->command); record->inPass = false; return {};
    }
    Result<void> setPipeline(const PipelinePtr& pipeline) override {
        if (auto ok = ready(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "setPipeline requires a Graphics command list");
        if (!record->inPass) return fail(ErrorCode::InvalidState, "Pipeline binding requires an active render pass");
        auto value = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        if (!value) return fail(pipeline ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument, "Expected a Vulkan graphics pipeline");
        if (value->owner != owner) return fail(ErrorCode::WrongDevice, "Pipeline belongs to another generation");
        if (value->native->isCompute) return fail(ErrorCode::InvalidArgument, "Graphics setPipeline rejects a compute pipeline");
        if (value->native->colors != record->passColors || value->native->depth.has_value() != record->passDepth) return fail(ErrorCode::InvalidArgument, "Pipeline outputs do not match active pass");
        if (value->native->sampleCount != record->passSampleCount) return fail(ErrorCode::InvalidArgument, "Pipeline sample count does not match active pass");
        if (std::find(record->pipelines.begin(), record->pipelines.end(), value->native) == record->pipelines.end()) record->pipelines.push_back(value->native);
        record->selectedPipeline = value->native;
        record->selectedBindings = {};
        owner->native->CmdBindPipeline(record->command, VK_PIPELINE_BIND_POINT_GRAPHICS, value->native->pipeline); return {};
    }
    Result<void> bindTexture(const TexturePtr& texture) override {
        if (auto ok = ready(); !ok) return ok;
        if (!record->selectedPipeline || !record->selectedPipeline->sampled) return fail(ErrorCode::InvalidState, "bindTexture requires sampledTexture pipeline");
        return bindResources({{{0, 0, texture}}, {}});
    }
    Result<void> bindTextureView(const TextureViewPtr& view) override {
        if (auto ok = ready(); !ok) return ok;
        if (!record->selectedPipeline || !record->selectedPipeline->sampled) return fail(ErrorCode::InvalidState, "bindTextureView requires sampledTexture pipeline");
        return bindResources({{{0, 0, nullptr, view}}, {}});
    }
    Result<void> bindResources(const ResourceBindings& bindings) override {
        if (auto ok = ready(); !ok) return ok;
        const auto p = record->selectedPipeline;
        if (!p) return fail(ErrorCode::InvalidState, "Resource binding requires a pipeline");
        if (p->descriptorSize > 256 - record->descriptors || record->snapshots.size() >= 256) return fail(ErrorCode::InvalidState, "Command descriptor capacity exhausted; split command list");
        if (!bindings.storageBuffers.empty() || !bindings.storageTextures.empty()) return fail(ErrorCode::InvalidArgument, "Storage bindings require bindComputeResources");
        if (bindings.textures.size() + bindings.constants.size() != p->descriptorSize) return fail(ErrorCode::InvalidArgument, "Every resource array element must be bound exactly once");
        auto snapshot = std::make_unique<NativeBindings>(); snapshot->native = owner->native; snapshot->pipeline = p; snapshot->resources.resize(p->descriptorSize);
        std::vector<VkDescriptorImageInfo> images(p->descriptorSize);
        std::vector<VkDescriptorBufferInfo> buffers(p->descriptorSize);
        std::vector<VkWriteDescriptorSet> writes; writes.reserve(p->descriptorSize);
        auto slot = [&](unsigned binding, unsigned element, BindingType type) -> Result<unsigned> {
            if (binding >= p->bindings.entries.size()) return fail(ErrorCode::InvalidArgument, "Unknown resource entry");
            const auto& e = p->bindings.entries[binding];
            if (e.type != type || element >= e.count) return fail(ErrorCode::InvalidArgument, "Resource type/element mismatch");
            unsigned at = element; for (unsigned i = 0; i < binding; ++i) at += p->bindings.entries[i].count;
            if (snapshot->resources[at]) return fail(ErrorCode::InvalidArgument, "Duplicate resource element");
            return at;
        };
        for (const auto& b : bindings.textures) {
            if (b.texture && b.view) return fail(ErrorCode::InvalidArgument, "Specify either texture or view in TextureBinding, not both");
            std::shared_ptr<Allocation> a;
            VkImageView nativeView{};
            TextureDimension resolvedDim{TextureDimension::Texture2D};
            if (b.view) {
                auto v = textureView(b.view); if (!v) return std::unexpected(v.error());
                if ((*v)->desc.usage != TextureViewUsage::ShaderRead) return fail(ErrorCode::InvalidArgument, "Texture binding view requires ShaderRead usage");
                a = (*v)->allocation;
                nativeView = (*v)->view;
                resolvedDim = (*v)->desc.dimension.value_or(a->textureDesc.dimension);
                snapshot->textureViews.push_back(*v);
            } else {
                auto tex = texture(b.texture); if (!tex) return std::unexpected(tex.error());
                a = *tex;
                nativeView = a->view;
                resolvedDim = a->textureDesc.dimension;
            }
            auto at = slot(b.binding, b.element, bindingTypeForDimension(resolvedDim)); if (!at) return std::unexpected(at.error());
            if (a->swapchain) return fail(ErrorCode::InvalidArgument, "Swapchain backbuffer sampling unsupported");
            if (a->textureDesc.depthStencil) return fail(ErrorCode::InvalidArgument, "Depth texture sampling unsupported");
            if (a->textureDesc.sampleCount > 1u) return fail(ErrorCode::Unsupported, "Multisampled texture sampling unsupported");
            if (shadow(a) != ResourceState::ShaderRead) return fail(ErrorCode::InvalidState, "Texture binding requires ShaderRead");
            const auto use = find(a);
            if (!(use ? use->contentsInitialized : a->contentsInitialized)) return fail(ErrorCode::InvalidState, "Sampled texture must have initialized contents");
            if (p->linear) {
                VkFormatProperties properties{};
                owner->native->instance->GetPhysicalDeviceFormatProperties(owner->physical, textureFormat(a->textureDesc.format), &properties);
                if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) return fail(ErrorCode::Unsupported, "Texture lacks linear filtering support");
            }
            snapshot->resources[*at] = a;
            images[*at] = {VK_NULL_HANDLE, nativeView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstBinding = b.binding; w.dstArrayElement = b.element; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w.pImageInfo = &images[*at]; writes.push_back(w);
        }
        for (const auto& b : bindings.constants) {
            auto at = slot(b.binding, b.element, BindingType::ConstantBuffer); if (!at) return std::unexpected(at.error());
            auto a = buffer(b.buffer); if (!a) return std::unexpected(a.error());
            if ((*a)->desc.memory == Memory::Readback || b.offset % 256 || !b.size || b.size % 256 || b.size > 65536 || b.offset > (*a)->desc.size || b.size > (*a)->desc.size - b.offset || b.size < p->minimumBytes[b.binding])
                return fail(ErrorCode::InvalidArgument, "Invalid constant buffer view alignment/range/shader size");
            if (b.offset % owner->limits.minUniformBufferOffsetAlignment || b.size > owner->limits.maxUniformBufferRange) return fail(ErrorCode::Unsupported, "Constant view exceeds adapter alignment/range");
            if (shadow(*a) != ResourceState::ConstantBuffer) return fail(ErrorCode::InvalidState, "Constant binding requires ConstantBuffer state");
            snapshot->resources[*at] = *a; buffers[*at] = {(*a)->buffer, b.offset, b.size};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstBinding = b.binding; w.dstArrayElement = b.element; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w.pBufferInfo = &buffers[*at]; writes.push_back(w);
        }
        for (const auto& a : snapshot->resources) if (!a) return fail(ErrorCode::InvalidArgument, "Missing resource element");
        // No recording references/versions or descriptor budget are consumed by validation failures.
        record->uses.reserve(record->uses.size() + p->descriptorSize); record->snapshots.reserve(record->snapshots.size() + 1);
        if (p->setLayout) {
            std::array<unsigned, 3> counts{};
            for (const auto& e : p->bindings.entries) counts[isSampledTextureBinding(e.type) ? 0 : 1] += e.count;
            counts[2] = static_cast<unsigned>(p->samplers.size());
            std::vector<VkDescriptorPoolSize> sizes;
            const VkDescriptorType types[]{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLER};
            for (unsigned i = 0; i < 3; ++i) if (counts[i]) sizes.push_back({types[i], counts[i]});
            VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool.maxSets = 1; pool.poolSizeCount = static_cast<unsigned>(sizes.size()); pool.pPoolSizes = sizes.data();
            if (auto ok = owner->accept(vulkan::acquireOutput(snapshot->pool, owner->native->CreateDescriptorPool, owner->native->handle, &pool, nullptr), "vkCreateDescriptorPool"); !ok) return ok;
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; allocate.descriptorPool = snapshot->pool; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &p->setLayout;
            if (auto ok = owner->accept(vulkan::acquireOutput(snapshot->set, owner->native->AllocateDescriptorSets, owner->native->handle, &allocate), "vkAllocateDescriptorSets"); !ok) return ok;
            for (auto& w : writes) w.dstSet = snapshot->set;
            owner->native->UpdateDescriptorSets(owner->native->handle, static_cast<unsigned>(writes.size()), writes.data(), 0, nullptr);
            owner->native->CmdBindDescriptorSets(record->command, VK_PIPELINE_BIND_POINT_GRAPHICS, p->layout, 0, 1, &snapshot->set, 0, nullptr);
        }
        for (const auto& a : snapshot->resources) retain(a);
        record->descriptors += p->descriptorSize; record->selectedBindings = snapshot.get(); record->snapshots.push_back(std::move(snapshot)); return {};
    }
    Result<void> bindingsReady() {
        if (record->passDepth && record->depthReadOnly && record->selectedPipeline->depthState.writeEnabled) return fail(ErrorCode::InvalidState, "Read-only depth rejects writing pipeline");
        if (!record->selectedPipeline->setLayout) return {};
        if (!record->selectedBindings) return fail(ErrorCode::InvalidState, "Draw requires complete resource snapshot");
        for (const auto& a : record->selectedBindings->resources)
            if (shadow(a) != (a->image ? ResourceState::ShaderRead : ResourceState::ConstantBuffer)) return fail(ErrorCode::InvalidState, "Bound resource read state changed");
        return {};
    }
    Result<void> bindVertexBuffer(std::uint32_t slot, const BufferPtr& ptr, std::uint32_t stride, std::uint64_t offset, std::uint64_t size) override {
        if (auto ok = ready(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "bindVertexBuffer requires a Graphics command list");
        auto a = buffer(ptr); if (!a) return std::unexpected(a.error());
        if (slot >= 32 || !stride || stride > 2048 || offset > (*a)->desc.size || size > (*a)->desc.size - offset || (*a)->desc.memory == Memory::Readback)
            return fail(ErrorCode::InvalidArgument, "Invalid vertex buffer view");
        if (slot >= owner->limits.maxVertexInputBindings) return fail(ErrorCode::Unsupported, "Vertex slot exceeds adapter limit");
        const auto bytes = size ? size : (*a)->desc.size - offset;
        if (!bytes || bytes > UINT32_MAX) return fail(ErrorCode::InvalidArgument, "Vertex view must contain 1..UINT32_MAX bytes");
        if (!vertexState(shadow(*a))) return fail(ErrorCode::InvalidState, "Vertex buffer requires vertex read state");
        record->uses.reserve(record->uses.size() + 1); retain(*a);
        record->vertices[slot] = {*a, offset, bytes, stride};
        const VkDeviceSize nativeOffset = offset; owner->native->CmdBindVertexBuffers(record->command, slot, 1, &(*a)->buffer, &nativeOffset); return {};
    }
    Result<void> bindIndexBuffer(const BufferPtr& ptr, IndexFormat format, std::uint64_t offset, std::uint64_t size) override {
        if (auto ok = ready(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "bindIndexBuffer requires a Graphics command list");
        auto a = buffer(ptr); if (!a) return std::unexpected(a.error());
        if (format != IndexFormat::UInt16 && format != IndexFormat::UInt32) return fail(ErrorCode::InvalidArgument, "Invalid index format");
        if (format == IndexFormat::UInt32 && !owner->fullDrawIndexUint32) return fail(ErrorCode::Unsupported, "Adapter lacks full uint32 index range");
        const auto element = format == IndexFormat::UInt16 ? 2u : 4u;
        if (offset > (*a)->desc.size || size > (*a)->desc.size - offset || offset % element || (*a)->desc.memory == Memory::Readback)
            return fail(ErrorCode::InvalidArgument, "Invalid index view offset/memory/range");
        const auto bytes = size ? size : (*a)->desc.size - offset;
        if (!bytes || bytes > UINT32_MAX || bytes % element) return fail(ErrorCode::InvalidArgument, "Invalid index view size");
        if (!indexState(shadow(*a))) return fail(ErrorCode::InvalidState, "Index buffer requires index read state");
        record->uses.reserve(record->uses.size() + 1); retain(*a); record->indices = {*a, bytes, element};
        owner->native->CmdBindIndexBuffer(record->command, (*a)->buffer, offset, format == IndexFormat::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32); return {};
    }
    Result<void> inputReady(std::uint32_t count, std::uint32_t instances, std::uint32_t firstVertex, std::uint32_t firstInstance, bool indexed = false, std::int32_t baseVertex = 0) {
        for (const auto& input : record->selectedPipeline->inputs) {
            const auto& view = record->vertices[input.slot];
            if (!view.allocation || view.stride != input.stride || !vertexState(shadow(view.allocation))) return fail(ErrorCode::InvalidState, "Missing/mismatched/stale vertex binding");
            if (view.offset % input.alignment || view.bytes < input.extent) return fail(ErrorCode::InvalidArgument, "Vertex view alignment/extent mismatch");
            const auto last = (view.bytes - input.extent) / input.stride;
            if (input.rate == VertexInputRate::Instance) {
                if (std::uint64_t(firstInstance) + instances - 1 > last) return fail(ErrorCode::InvalidArgument, "Instance input exceeds view");
            } else if (!indexed) {
                if (std::uint64_t(firstVertex) + count - 1 > last) return fail(ErrorCode::InvalidArgument, "Vertex input exceeds view");
            } else if (baseVertex > 0 && std::uint32_t(baseVertex) > last) return fail(ErrorCode::InvalidArgument, "Base vertex exceeds view");
        }
        return {};
    }
    Result<void> setViewport(const Viewport& viewport) override {
        if (auto ok = ready(); !ok) return ok;
        if (!record->inPass) return fail(ErrorCode::InvalidState, "Viewport requires an active render pass");
        if (auto ok = validateViewport(viewport, record->width, record->height); !ok) return ok;
        // Vulkan 1.1 negative-height viewport aligns NDC +Y to top-left screen coordinates.
        const VkViewport nativeViewport{
            viewport.x, viewport.y + viewport.height, viewport.width, -viewport.height,
            viewport.minDepth, viewport.maxDepth};
        owner->native->CmdSetViewport(record->command, 0, 1, &nativeViewport);
        return {};
    }
    Result<void> setScissor(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) override {
        if (auto ok = ready(); !ok) return ok;
        if (!record->inPass) return fail(ErrorCode::InvalidState, "Scissor requires an active render pass");
        if (!width || !height || x > record->width || width > record->width - x || y > record->height || height > record->height - y)
            return fail(ErrorCode::InvalidArgument, "Scissor extent outside active attachment");
        const VkRect2D scissor{{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)}, {width, height}};
        owner->native->CmdSetScissor(record->command, 0, 1, &scissor); return {};
    }
    Result<void> draw(std::uint32_t vertices, std::uint32_t instances, std::uint32_t firstVertex, std::uint32_t firstInstance) override {
        if (auto ok = ready(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "draw requires a Graphics command list");
        if (!record->inPass || !record->selectedPipeline) return fail(ErrorCode::InvalidState, "Draw requires active pass and compatible pipeline");
        if (!vertices || !instances || vertices > UINT32_MAX - firstVertex || instances > UINT32_MAX - firstInstance)
            return fail(ErrorCode::InvalidArgument, "Invalid draw count/index range");
        if (auto ok = inputReady(vertices, instances, firstVertex, firstInstance); !ok) return ok;
        if (auto ok = bindingsReady(); !ok) return ok;
        owner->native->CmdDraw(record->command, vertices, instances, firstVertex, firstInstance);
        if (owner->profiler) owner->profiler->recordDraw(false, instances);
        return {};
    }
    Result<void> drawIndexed(std::uint32_t count, std::uint32_t instances, std::uint32_t firstIndex, std::int32_t baseVertex, std::uint32_t firstInstance) override {
        if (auto ok = ready(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "drawIndexed requires a Graphics command list");
        if (!record->inPass || !record->selectedPipeline) return fail(ErrorCode::InvalidState, "Indexed draw requires active pass and pipeline");
        if (!count || !instances || instances > UINT32_MAX - firstInstance) return fail(ErrorCode::InvalidArgument, "Invalid indexed draw counts");
        const auto& view = record->indices;
        if (!view.allocation || !indexState(shadow(view.allocation))) return fail(ErrorCode::InvalidState, "Missing/stale index binding");
        if (std::uint64_t(firstIndex) + count > view.bytes / view.element) return fail(ErrorCode::InvalidArgument, "Index range exceeds view");
        if (auto ok = inputReady(0, instances, 0, firstInstance, true, baseVertex); !ok) return ok;
        if (auto ok = bindingsReady(); !ok) return ok;
        owner->native->CmdDrawIndexed(record->command, count, instances, firstIndex, baseVertex, firstInstance);
        if (owner->profiler) owner->profiler->recordDraw(true, instances);
        return {};
    }
    Result<void> indirectInputReady() {
        for (const auto& input : record->selectedPipeline->inputs) {
            const auto& view = record->vertices[input.slot];
            if (!view.allocation || view.stride != input.stride || !vertexState(shadow(view.allocation)))
                return fail(ErrorCode::InvalidState, "Missing/mismatched/stale vertex binding");
            if (view.offset % input.alignment || view.bytes < input.extent)
                return fail(ErrorCode::InvalidArgument, "Vertex view alignment/extent mismatch");
        }
        return {};
    }
    Result<void> drawIndirect(const BufferPtr& argsBuffer, std::uint64_t argsOffset, std::uint32_t drawCount, std::uint32_t stride) override {
        if (auto ok = ready(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "drawIndirect requires a Graphics command list");
        if (!record->inPass || !record->selectedPipeline) return fail(ErrorCode::InvalidState, "drawIndirect requires active pass and pipeline");
        if (!drawCount || drawCount > owner->maxDrawIndirectCount)
            return fail(ErrorCode::InvalidArgument, "drawIndirect drawCount out of supported range");
        if (drawCount > 1u && !owner->multiDrawIndirect)
            return fail(ErrorCode::Unsupported, "Multi-Draw Indirect is unsupported on this adapter");
        if (stride < sizeof(DrawIndirectCommand) || (stride % 4u) != 0u || (argsOffset % 4u) != 0u)
            return fail(ErrorCode::InvalidArgument, "drawIndirect offset and stride must be 4-byte aligned and >= 16 bytes");
        auto argBuf = buffer(argsBuffer); if (!argBuf) return std::unexpected(argBuf.error());
        if ((*argBuf)->desc.memory == Memory::Readback)
            return fail(ErrorCode::InvalidArgument, "Readback buffer cannot be used as an indirect argument buffer");
        if (shadow(*argBuf) != ResourceState::IndirectArgument)
            return fail(ErrorCode::InvalidState, "drawIndirect requires ResourceState::IndirectArgument");
        const std::uint64_t spanBytes = static_cast<std::uint64_t>(drawCount - 1u) * stride + sizeof(DrawIndirectCommand);
        if (argsOffset > (*argBuf)->desc.size || spanBytes > (*argBuf)->desc.size - argsOffset)
            return fail(ErrorCode::InvalidArgument, "drawIndirect argument range exceeds buffer size");
        if (auto ok = indirectInputReady(); !ok) return ok;
        if (auto ok = bindingsReady(); !ok) return ok;
        record->uses.reserve(record->uses.size() + 1);
        retain(*argBuf);
        owner->native->CmdDrawIndirect(record->command, (*argBuf)->buffer, argsOffset, drawCount, stride);
        if (owner->profiler) owner->profiler->recordDraw(false, drawCount);
        return {};
    }
    Result<void> drawIndexedIndirect(const BufferPtr& argsBuffer, std::uint64_t argsOffset, std::uint32_t drawCount, std::uint32_t stride) override {
        if (auto ok = ready(); !ok) return ok;
        if (qType != QueueType::Graphics) return fail(ErrorCode::InvalidArgument, "drawIndexedIndirect requires a Graphics command list");
        if (!record->inPass || !record->selectedPipeline) return fail(ErrorCode::InvalidState, "drawIndexedIndirect requires active pass and pipeline");
        if (!drawCount || drawCount > owner->maxDrawIndirectCount)
            return fail(ErrorCode::InvalidArgument, "drawIndexedIndirect drawCount out of supported range");
        if (drawCount > 1u && !owner->multiDrawIndirect)
            return fail(ErrorCode::Unsupported, "Multi-Draw Indirect is unsupported on this adapter");
        if (stride < sizeof(DrawIndexedIndirectCommand) || (stride % 4u) != 0u || (argsOffset % 4u) != 0u)
            return fail(ErrorCode::InvalidArgument, "drawIndexedIndirect offset and stride must be 4-byte aligned and >= 20 bytes");
        const auto& view = record->indices;
        if (!view.allocation || !indexState(shadow(view.allocation)))
            return fail(ErrorCode::InvalidState, "Missing/stale index binding");
        auto argBuf = buffer(argsBuffer); if (!argBuf) return std::unexpected(argBuf.error());
        if ((*argBuf)->desc.memory == Memory::Readback)
            return fail(ErrorCode::InvalidArgument, "Readback buffer cannot be used as an indirect argument buffer");
        if (shadow(*argBuf) != ResourceState::IndirectArgument)
            return fail(ErrorCode::InvalidState, "drawIndexedIndirect requires ResourceState::IndirectArgument");
        const std::uint64_t spanBytes = static_cast<std::uint64_t>(drawCount - 1u) * stride + sizeof(DrawIndexedIndirectCommand);
        if (argsOffset > (*argBuf)->desc.size || spanBytes > (*argBuf)->desc.size - argsOffset)
            return fail(ErrorCode::InvalidArgument, "drawIndexedIndirect argument range exceeds buffer size");
        if (auto ok = indirectInputReady(); !ok) return ok;
        if (auto ok = bindingsReady(); !ok) return ok;
        record->uses.reserve(record->uses.size() + 1);
        retain(*argBuf);
        owner->native->CmdDrawIndexedIndirect(record->command, (*argBuf)->buffer, argsOffset, drawCount, stride);
        if (owner->profiler) owner->profiler->recordDraw(true, drawCount);
        return {};
    }
    Result<void> setComputePipeline(const PipelinePtr& pipeline) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::InvalidArgument, "Compute operations are not supported on a Copy command list");
        auto value = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        if (!value) return fail(pipeline ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument, "Expected a Vulkan compute pipeline");
        if (value->owner != owner) return fail(ErrorCode::WrongDevice, "Compute pipeline belongs to another generation");
        if (!value->native->isCompute) return fail(ErrorCode::InvalidArgument, "setComputePipeline requires a compute pipeline");
        if (std::find(record->pipelines.begin(), record->pipelines.end(), value->native) == record->pipelines.end()) {
            record->pipelines.push_back(value->native);
        }
        record->selectedComputePipeline = value->native;
        record->selectedComputeBindings = {};
        owner->native->CmdBindPipeline(record->command, VK_PIPELINE_BIND_POINT_COMPUTE, value->native->pipeline);
        return {};
    }
    Result<void> bindComputeResources(const ResourceBindings& bindings) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::InvalidArgument, "Compute operations are not supported on a Copy command list");
        const auto p = record->selectedComputePipeline;
        if (!p) return fail(ErrorCode::InvalidState, "Compute resource binding requires a compute pipeline");
        if (p->descriptorSize > 256 - record->descriptors || record->snapshots.size() >= 256) {
            return fail(ErrorCode::InvalidState, "Command descriptor capacity exhausted; split command list");
        }
        const std::size_t totalBindings = bindings.textures.size() + bindings.constants.size() +
            bindings.storageBuffers.size() + bindings.storageTextures.size();
        if (totalBindings != p->descriptorSize) {
            return fail(ErrorCode::InvalidArgument, "Every compute resource array element must be bound exactly once");
        }
        auto snapshot = std::make_unique<NativeBindings>();
        snapshot->native = owner->native;
        snapshot->pipeline = p;
        snapshot->resources.resize(p->descriptorSize);
        snapshot->expectedStates.resize(p->descriptorSize, ResourceState::Common);
        std::vector<VkDescriptorImageInfo> images(p->descriptorSize);
        std::vector<VkDescriptorBufferInfo> buffers(p->descriptorSize);
        std::vector<VkWriteDescriptorSet> writes;
        writes.reserve(p->descriptorSize);
        std::vector<std::shared_ptr<Allocation>> storageImagesToInit;

        auto slot = [&](unsigned binding, unsigned element, BindingType type) -> Result<unsigned> {
            if (binding >= p->bindings.entries.size()) return fail(ErrorCode::InvalidArgument, "Unknown compute resource entry");
            const auto& e = p->bindings.entries[binding];
            if (e.type != type || element >= e.count) return fail(ErrorCode::InvalidArgument, "Compute resource type/element mismatch");
            unsigned at = element;
            for (unsigned i = 0; i < binding; ++i) at += p->bindings.entries[i].count;
            if (snapshot->resources[at]) return fail(ErrorCode::InvalidArgument, "Duplicate compute resource element");
            return at;
        };

        for (const auto& b : bindings.textures) {
            if (b.texture && b.view) return fail(ErrorCode::InvalidArgument, "Specify either texture or view in TextureBinding, not both");
            std::shared_ptr<Allocation> a;
            VkImageView nativeView{};
            TextureDimension resolvedDim{TextureDimension::Texture2D};
            if (b.view) {
                auto v = textureView(b.view); if (!v) return std::unexpected(v.error());
                if ((*v)->desc.usage != TextureViewUsage::ShaderRead) return fail(ErrorCode::InvalidArgument, "Texture binding view requires ShaderRead usage");
                a = (*v)->allocation; nativeView = (*v)->view;
                resolvedDim = (*v)->desc.dimension.value_or(a->textureDesc.dimension);
                snapshot->textureViews.push_back(*v);
            } else {
                auto tex = texture(b.texture); if (!tex) return std::unexpected(tex.error());
                a = *tex; nativeView = a->view;
                resolvedDim = a->textureDesc.dimension;
            }
            auto at = slot(b.binding, b.element, bindingTypeForDimension(resolvedDim)); if (!at) return std::unexpected(at.error());
            if (a->swapchain || a->textureDesc.depthStencil) return fail(ErrorCode::InvalidArgument, "Sampled compute texture must be a non-swapchain color texture");
            if (shadow(a) != ResourceState::ShaderRead) return fail(ErrorCode::InvalidState, "Compute texture binding requires ShaderRead");
            const auto use = find(a);
            if (!(use ? use->contentsInitialized : a->contentsInitialized)) return fail(ErrorCode::InvalidState, "Sampled compute texture must have initialized contents");
            snapshot->resources[*at] = a;
            snapshot->expectedStates[*at] = ResourceState::ShaderRead;
            images[*at] = {VK_NULL_HANDLE, nativeView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstBinding = b.binding; w.dstArrayElement = b.element; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w.pImageInfo = &images[*at];
            writes.push_back(w);
        }
        for (const auto& b : bindings.constants) {
            auto at = slot(b.binding, b.element, BindingType::ConstantBuffer); if (!at) return std::unexpected(at.error());
            auto a = buffer(b.buffer); if (!a) return std::unexpected(a.error());
            if ((*a)->desc.memory == Memory::Readback || b.offset % 256 || !b.size || b.size % 256 || b.size > 65536 ||
                b.offset > (*a)->desc.size || b.size > (*a)->desc.size - b.offset || b.size < p->minimumBytes[b.binding])
                return fail(ErrorCode::InvalidArgument, "Invalid compute constant buffer view");
            if (b.offset % owner->limits.minUniformBufferOffsetAlignment || b.size > owner->limits.maxUniformBufferRange)
                return fail(ErrorCode::Unsupported, "Constant view exceeds adapter alignment/range");
            if (shadow(*a) != ResourceState::ConstantBuffer) return fail(ErrorCode::InvalidState, "Compute constant binding requires ConstantBuffer state");
            snapshot->resources[*at] = *a;
            snapshot->expectedStates[*at] = ResourceState::ConstantBuffer;
            buffers[*at] = {(*a)->buffer, b.offset, b.size};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstBinding = b.binding; w.dstArrayElement = b.element; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w.pBufferInfo = &buffers[*at];
            writes.push_back(w);
        }
        for (const auto& b : bindings.storageBuffers) {
            auto at = slot(b.binding, b.element, BindingType::StorageBuffer); if (!at) return std::unexpected(at.error());
            auto a = buffer(b.buffer); if (!a) return std::unexpected(a.error());
            if (!(*a)->desc.storage || (*a)->desc.memory != Memory::Device) {
                return fail(ErrorCode::InvalidArgument, "StorageBuffer binding requires a device storage buffer");
            }
            if (b.offset % 256 || b.offset > (*a)->desc.size) {
                return fail(ErrorCode::InvalidArgument, "StorageBuffer offset must be a 256-byte multiple within bounds");
            }
            const std::uint64_t size = b.size ? b.size : (*a)->desc.size - b.offset;
            if (!size || size % 4 || size > (*a)->desc.size - b.offset) {
                return fail(ErrorCode::InvalidArgument, "StorageBuffer size must be a positive 4-byte multiple within bounds");
            }
            if ((owner->limits.minStorageBufferOffsetAlignment && b.offset % owner->limits.minStorageBufferOffsetAlignment) ||
                size > owner->limits.maxStorageBufferRange) {
                return fail(ErrorCode::Unsupported, "StorageBuffer view exceeds adapter alignment/range");
            }
            if (shadow(*a) != ResourceState::UnorderedAccess) {
                return fail(ErrorCode::InvalidState, "StorageBuffer binding requires UnorderedAccess state");
            }
            snapshot->resources[*at] = *a;
            snapshot->expectedStates[*at] = ResourceState::UnorderedAccess;
            buffers[*at] = {(*a)->buffer, b.offset, size};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstBinding = b.binding; w.dstArrayElement = b.element; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = &buffers[*at];
            writes.push_back(w);
        }
        for (const auto& b : bindings.storageTextures) {
            auto at = slot(b.binding, b.element, BindingType::StorageTexture2D); if (!at) return std::unexpected(at.error());
            if (b.texture && b.view) return fail(ErrorCode::InvalidArgument, "Specify either texture or view in StorageTextureBinding, not both");
            std::shared_ptr<Allocation> a;
            VkImageView nativeView{};
            if (b.view) {
                auto v = textureView(b.view); if (!v) return std::unexpected(v.error());
                if ((*v)->desc.usage != TextureViewUsage::Storage) return fail(ErrorCode::InvalidArgument, "StorageTextureBinding view requires Storage usage");
                a = (*v)->allocation; nativeView = (*v)->view;
                snapshot->textureViews.push_back(*v);
            } else {
                auto tex = texture(b.texture); if (!tex) return std::unexpected(tex.error());
                a = *tex; nativeView = a->view;
            }
            if (!a->textureDesc.storage || a->textureDesc.depthStencil) {
                return fail(ErrorCode::InvalidArgument, "StorageTextureBinding requires a color storage texture");
            }
            if (shadow(a) != ResourceState::UnorderedAccess) {
                return fail(ErrorCode::InvalidState, "StorageTextureBinding requires UnorderedAccess state");
            }
            snapshot->resources[*at] = a;
            snapshot->expectedStates[*at] = ResourceState::UnorderedAccess;
            storageImagesToInit.push_back(a);
            images[*at] = {VK_NULL_HANDLE, nativeView, VK_IMAGE_LAYOUT_GENERAL};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstBinding = b.binding; w.dstArrayElement = b.element; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w.pImageInfo = &images[*at];
            writes.push_back(w);
        }
        for (const auto& a : snapshot->resources) if (!a) return fail(ErrorCode::InvalidArgument, "Missing compute resource element");

        record->uses.reserve(record->uses.size() + p->descriptorSize);
        record->snapshots.reserve(record->snapshots.size() + 1);
        if (p->setLayout) {
            std::array<unsigned, 5> counts{};
            for (const auto& e : p->bindings.entries) {
                const unsigned idx = isSampledTextureBinding(e.type) ? 0 :
                                     e.type == BindingType::ConstantBuffer ? 1 :
                                     e.type == BindingType::StorageBuffer ? 2 : 3;
                counts[idx] += e.count;
            }
            counts[4] = static_cast<unsigned>(p->samplers.size());
            std::vector<VkDescriptorPoolSize> sizes;
            const VkDescriptorType types[]{
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                VK_DESCRIPTOR_TYPE_SAMPLER
            };
            for (unsigned i = 0; i < 5; ++i) if (counts[i]) sizes.push_back({types[i], counts[i]});
            VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pool.maxSets = 1;
            pool.poolSizeCount = static_cast<unsigned>(sizes.size());
            pool.pPoolSizes = sizes.data();
            if (auto ok = owner->accept(vulkan::acquireOutput(snapshot->pool, owner->native->CreateDescriptorPool, owner->native->handle, &pool, nullptr), "vkCreateDescriptorPool"); !ok) return ok;
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocate.descriptorPool = snapshot->pool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &p->setLayout;
            if (auto ok = owner->accept(vulkan::acquireOutput(snapshot->set, owner->native->AllocateDescriptorSets, owner->native->handle, &allocate), "vkAllocateDescriptorSets"); !ok) return ok;
            for (auto& w : writes) w.dstSet = snapshot->set;
            owner->native->UpdateDescriptorSets(owner->native->handle, static_cast<unsigned>(writes.size()), writes.data(), 0, nullptr);
            owner->native->CmdBindDescriptorSets(record->command, VK_PIPELINE_BIND_POINT_COMPUTE, p->layout, 0, 1, &snapshot->set, 0, nullptr);
        }
        for (std::size_t i = 0; i < snapshot->resources.size(); ++i) {
            retain(snapshot->resources[i], snapshot->expectedStates[i] == ResourceState::UnorderedAccess);
        }
        for (const auto& a : storageImagesToInit) retain(a, true).contentsInitialized = true;
        record->descriptors += p->descriptorSize;
        record->selectedComputeBindings = snapshot.get();
        record->snapshots.push_back(std::move(snapshot));
        return {};
    }
    Result<void> dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::InvalidArgument, "Compute operations are not supported on a Copy command list");
        const auto p = record->selectedComputePipeline;
        if (!p) return fail(ErrorCode::InvalidState, "dispatch requires an active compute pipeline");
        if (!groupCountX || !groupCountY || !groupCountZ ||
            groupCountX > owner->limits.maxComputeWorkGroupCount[0] ||
            groupCountY > owner->limits.maxComputeWorkGroupCount[1] ||
            groupCountZ > owner->limits.maxComputeWorkGroupCount[2]) {
            return fail(ErrorCode::InvalidArgument, "dispatch group counts must be positive and within adapter limits");
        }
        if (p->setLayout) {
            if (!record->selectedComputeBindings) return fail(ErrorCode::InvalidState, "dispatch requires complete bindComputeResources snapshot");
            for (std::size_t i = 0; i < record->selectedComputeBindings->resources.size(); ++i) {
                if (shadow(record->selectedComputeBindings->resources[i]) != record->selectedComputeBindings->expectedStates[i]) {
                    return fail(ErrorCode::InvalidState, "Bound compute resource left its required state");
                }
            }
        }
        owner->native->CmdDispatch(record->command, groupCountX, groupCountY, groupCountZ);
        if (owner->profiler) owner->profiler->recordDispatch();
        return {};
    }
    Result<void> dispatchIndirect(const BufferPtr& argsBuffer, std::uint64_t argsOffset) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::InvalidArgument, "Compute operations are not supported on a Copy command list");
        const auto p = record->selectedComputePipeline;
        if (!p) return fail(ErrorCode::InvalidState, "dispatchIndirect requires an active compute pipeline");
        if ((argsOffset % 4u) != 0u) return fail(ErrorCode::InvalidArgument, "dispatchIndirect offset must be 4-byte aligned");
        if (p->setLayout) {
            if (!record->selectedComputeBindings) return fail(ErrorCode::InvalidState, "dispatchIndirect requires complete bindComputeResources snapshot");
            for (std::size_t i = 0; i < record->selectedComputeBindings->resources.size(); ++i) {
                if (shadow(record->selectedComputeBindings->resources[i]) != record->selectedComputeBindings->expectedStates[i]) {
                    return fail(ErrorCode::InvalidState, "Bound compute resource left its required state");
                }
            }
        }
        auto argBuf = buffer(argsBuffer); if (!argBuf) return std::unexpected(argBuf.error());
        if ((*argBuf)->desc.memory == Memory::Readback)
            return fail(ErrorCode::InvalidArgument, "Readback buffer cannot be used as an indirect argument buffer");
        if (shadow(*argBuf) != ResourceState::IndirectArgument)
            return fail(ErrorCode::InvalidState, "dispatchIndirect requires ResourceState::IndirectArgument");
        if (argsOffset > (*argBuf)->desc.size || sizeof(DispatchIndirectCommand) > (*argBuf)->desc.size - argsOffset)
            return fail(ErrorCode::InvalidArgument, "dispatchIndirect argument range exceeds buffer size");
        record->uses.reserve(record->uses.size() + 1);
        retain(*argBuf);
        owner->native->CmdDispatchIndirect(record->command, (*argBuf)->buffer, argsOffset);
        if (owner->profiler) owner->profiler->recordDispatch();
        return {};
    }
    Result<void> uavBarrier(const BufferPtr& value) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::InvalidArgument, "uavBarrier is not supported on a Copy command list");
        auto a = buffer(value); if (!a) return std::unexpected(a.error());
        if (!(*a)->desc.storage) return fail(ErrorCode::InvalidArgument, "uavBarrier requires a storage buffer");
        if (shadow(*a) != ResourceState::UnorderedAccess) return fail(ErrorCode::InvalidState, "uavBarrier requires UnorderedAccess state");
        record->uses.reserve(record->uses.size() + 1);
        retain(*a, true);
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = (*a)->buffer;
        barrier.size = VK_WHOLE_SIZE;
        owner->native->CmdPipelineBarrier(record->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
        if (owner->profiler) owner->profiler->recordUavBarrier();
        return {};
    }
    Result<void> uavBarrier(const TexturePtr& value) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::InvalidArgument, "uavBarrier is not supported on a Copy command list");
        auto a = texture(value); if (!a) return std::unexpected(a.error());
        if (!(*a)->textureDesc.storage) return fail(ErrorCode::InvalidArgument, "uavBarrier requires a storage texture");
        if (shadow(*a) != ResourceState::UnorderedAccess) return fail(ErrorCode::InvalidState, "uavBarrier requires UnorderedAccess state");
        record->uses.reserve(record->uses.size() + 1);
        retain(*a, true);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = (*a)->image;
        barrier.subresourceRange = {imageAspect(**a), 0, std::max(1u, (*a)->textureDesc.mipLevels), 0, std::max(1u, (*a)->textureDesc.arrayLayers)};
        owner->native->CmdPipelineBarrier(record->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        if (owner->profiler) owner->profiler->recordUavBarrier();
        return {};
    }
    Result<void> writeTimestamp(std::uint32_t index) override {
        if (auto ok = ready(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::Unsupported, "Timestamps are unsupported on Copy command lists");
        const VkQueryPool queryPool = record->queryHandle();
        if (!queryPool) return fail(ErrorCode::Unsupported, "Queue timestamps unsupported");
        if (index > 1 || record->timestampWritten[index] || record->timestampsResolved) return fail(ErrorCode::InvalidArgument, "Timestamp slot invalid or already written");
        if (!record->timestampWritten[0] && !record->timestampWritten[1]) record->timestampStartFirst = index == 0;
        owner->native->CmdWriteTimestamp(record->command, index ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, index);
        record->timestampWritten[index] = true; return {};
    }
    Result<void> resolveTimestamps() override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::Unsupported, "Timestamps are unsupported on Copy command lists");
        if (!record->queryHandle()) return fail(ErrorCode::Unsupported, "Queue timestamps unsupported");
        if (!record->timestampWritten[0] || !record->timestampWritten[1] || record->timestampsResolved) return fail(ErrorCode::InvalidState, "Two timestamps must be written once before resolving");
        // Vulkan copies query results directly to host memory after the submission fence.
        record->timestampsResolved = true; return {};
    }
    Result<double> elapsedGpuMilliseconds() override {
        if (auto ok = owner->pollQueue(qType); !ok) return std::unexpected(ok.error());
        if (qType == QueueType::Copy) return fail(ErrorCode::Unsupported, "Timestamps are unsupported on Copy command lists");
        auto& qSlot = owner->queues[queueIndex(qType)];
        if (!vulkan::timestampSupported(qSlot.timestampBits, owner->limits.timestampPeriod)) return fail(ErrorCode::Unsupported, "Queue timestamps unsupported");
        if (!record || !record->submitted || record->submitted > qSlot.completed.load(std::memory_order_acquire) || !record->timestampsResolved) return fail(ErrorCode::InvalidState, "GPU timestamps are not ready");
        if (record->timestampResult) return *record->timestampResult;
        const VkQueryPool queryPool = record->queryHandle();
        if (!queryPool) return fail(ErrorCode::Unsupported, "Queue timestamps unsupported");
        struct Query { std::uint64_t ticks{}, available{}; }; std::array<Query, 2> results;
        const auto r = owner->native->GetQueryPoolResults(owner->native->handle, queryPool, 0, 2, sizeof(results), results.data(), sizeof(Query), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (r == VK_NOT_READY) return fail(ErrorCode::InvalidState, "GPU query results unavailable");
        if (auto ok = owner->accept(r, "vkGetQueryPoolResults"); !ok) return std::unexpected(ok.error());
        if (!results[0].available || !results[1].available) return fail(ErrorCode::InvalidState, "GPU query availability missing");
        const auto host = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - record->submittedAt).count();
        auto elapsed = vulkan::timestampMilliseconds(results[0].ticks, results[1].ticks, qSlot.timestampBits, owner->limits.timestampPeriod, host, record->timestampStartFirst);
        if (!elapsed) return std::unexpected(elapsed.error());
        record->timestampResult = *elapsed; return *elapsed;
    }
    Result<void> beginTimestampScope(std::string_view name) override {
        if (auto ok = ready(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::Unsupported, "Timestamps are unsupported on Copy command lists");
        const VkQueryPool queryPool = record->queryHandle();
        if (!queryPool) return fail(ErrorCode::Unsupported, "Queue timestamps unsupported");
        if (name.empty() || name.size() > 256 || name.find('\0') != std::string_view::npos) {
            return fail(ErrorCode::InvalidArgument, "Timestamp scope name must be non-empty NUL-free UTF-8 up to 256 bytes");
        }
        if (record->scopesResolved || record->scopes.size() >= kMaxTimestampScopesPerCommandList ||
            record->nextScopeSlot + 2u > kMaxTimestampScopesPerCommandList * 2u) {
            return fail(ErrorCode::InvalidState, "Timestamp scope capacity exceeded or already resolved");
        }
        const std::uint32_t beginSlot = record->nextScopeSlot++;
        owner->native->CmdWriteTimestamp(record->command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, 2u + beginSlot);
        const std::uint32_t depth = static_cast<std::uint32_t>(record->openScopeStack.size());
        record->scopes.push_back(Recording::RecordedScope{std::string(name), depth, beginSlot, 0u, false});
        record->openScopeStack.push_back(static_cast<std::uint32_t>(record->scopes.size() - 1u));
        return {};
    }
    Result<void> endTimestampScope() override {
        if (auto ok = ready(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::Unsupported, "Timestamps are unsupported on Copy command lists");
        const VkQueryPool queryPool = record->queryHandle();
        if (!queryPool) return fail(ErrorCode::Unsupported, "Queue timestamps unsupported");
        if (record->openScopeStack.empty() || record->scopesResolved) {
            return fail(ErrorCode::InvalidState, "No open timestamp scope to end");
        }
        const std::uint32_t idx = record->openScopeStack.back();
        record->openScopeStack.pop_back();
        const std::uint32_t endSlot = record->nextScopeSlot++;
        owner->native->CmdWriteTimestamp(record->command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 2u + endSlot);
        record->scopes[idx].endSlot = endSlot;
        record->scopes[idx].closed = true;
        return {};
    }
    Result<void> resolveTimestampScopes() override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (qType == QueueType::Copy) return fail(ErrorCode::Unsupported, "Timestamps are unsupported on Copy command lists");
        if (!record->queryHandle()) return fail(ErrorCode::Unsupported, "Queue timestamps unsupported");
        if (record->scopes.empty() || !record->openScopeStack.empty() || record->scopesResolved) {
            return fail(ErrorCode::InvalidState, "Timestamp scopes must be non-empty, balanced, and unresolved before resolving");
        }
        record->scopesResolved = true;
        return {};
    }
    Result<std::vector<TimestampScopeResult>> timestampScopes() override {
        if (auto ok = owner->pollQueue(qType); !ok) return std::unexpected(ok.error());
        if (qType == QueueType::Copy) return fail(ErrorCode::Unsupported, "Timestamps are unsupported on Copy command lists");
        auto& qSlot = owner->queues[queueIndex(qType)];
        if (!vulkan::timestampSupported(qSlot.timestampBits, owner->limits.timestampPeriod)) return fail(ErrorCode::Unsupported, "Queue timestamps unsupported");
        if (!record || !record->submitted || record->submitted > qSlot.completed.load(std::memory_order_acquire) || !record->scopesResolved) {
            return fail(ErrorCode::InvalidState, "GPU timestamp scopes are not ready");
        }
        if (record->cachedScopeResults) return *record->cachedScopeResults;
        const VkQueryPool queryPool = record->queryHandle();
        if (!queryPool) return fail(ErrorCode::Unsupported, "Queue timestamps unsupported");
        struct Query { std::uint64_t ticks{}, available{}; };
        std::vector<Query> results(record->nextScopeSlot);
        const auto r = owner->native->GetQueryPoolResults(
            owner->native->handle, queryPool, 2u, record->nextScopeSlot,
            results.size() * sizeof(Query), results.data(), sizeof(Query),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (r == VK_NOT_READY) return fail(ErrorCode::InvalidState, "GPU query results unavailable");
        if (auto ok = owner->accept(r, "vkGetQueryPoolResults"); !ok) return std::unexpected(ok.error());

        const auto host = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - record->submittedAt).count();
        std::vector<TimestampScopeResult> out;
        out.reserve(record->scopes.size());
        for (const auto& s : record->scopes) {
            const auto& a = results[s.beginSlot];
            const auto& b = results[s.endSlot];
            if (!a.available || !b.available) {
                return fail(ErrorCode::InvalidState, "GPU query availability missing");
            }
            auto elapsed = vulkan::timestampMilliseconds(
                a.ticks, b.ticks, qSlot.timestampBits, owner->limits.timestampPeriod, host, true);
            if (!elapsed) return std::unexpected(elapsed.error());
            out.push_back(TimestampScopeResult{s.name, qType, s.depth, a.ticks, b.ticks, *elapsed});
        }
        record->cachedScopeResults = out;
        if (owner->profiler) owner->profiler->appendGpuScopes(out);
        return out;
    }
    Result<void> debugMarker(std::string_view text) override {
        if (auto ok = ready(); !ok) return ok;
        if (!owner->native->CmdInsertDebugUtilsLabelEXT) return fail(ErrorCode::Unsupported, "Vulkan debug-utils labels unavailable");
        if (!markerText(text)) return fail(ErrorCode::InvalidArgument, "Marker requires NUL-free UTF-8 and at most UINT32_MAX bytes");
        try {
            std::string name;
            if (!text.empty()) name.assign(text);
            const VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, nullptr, name.c_str(), {0, 0, 0, 0}};
            owner->native->CmdInsertDebugUtilsLabelEXT(record->command, &label);
            return {};
        } catch (const std::bad_alloc&) { return fail(ErrorCode::OutOfMemory, "Marker text allocation failed"); }
    }
};
struct VulkanCommandPool final : CommandPool {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativeCommandPool> pool;
    std::shared_ptr<int> token;
    ~VulkanCommandPool() override {
        if (pool) {
            std::scoped_lock lock(pool->mutex);
            pool->ownerAlive = false;
        }
    }
    QueueType queueType() const override { return pool->qType; }
    Result<std::unique_ptr<CommandList>> createCommandList() override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        std::scoped_lock lock(pool->mutex);
        if (pool->openCount > 0) {
            return fail(ErrorCode::InvalidState, "Cannot allocate from a CommandPool while a command list from it is recording");
        }
        VkCommandBuffer cmd{};
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = pool->handle;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        if (auto ok = owner->accept(vulkan::acquireOutput(cmd, owner->native->AllocateCommandBuffers, owner->native->handle, &allocate), "vkAllocateCommandBuffers"); !ok) {
            return std::unexpected(ok.error());
        }
        std::shared_ptr<NativeQueryPool> queries;
        const auto& qSlot = owner->queues[queueIndex(pool->qType)];
        if (pool->qType != QueueType::Copy && vulkan::timestampSupported(qSlot.timestampBits, owner->limits.timestampPeriod)) {
            queries = std::make_shared<NativeQueryPool>();
            queries->native = owner->native;
            VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = 2u + kMaxTimestampScopesPerCommandList * 2u;
            if (auto ok = owner->accept(vulkan::acquireOutput(queries->handle, owner->native->CreateQueryPool, owner->native->handle, &info, nullptr), "vkCreateQueryPool"); !ok) {
                owner->native->FreeCommandBuffers(owner->native->handle, pool->handle, 1, &cmd);
                return std::unexpected(ok.error());
            }
            std::erase_if(owner->queryPools, [](auto& w) { return w.expired(); });
            owner->queryPools.push_back(queries);
        }
        auto value = std::make_unique<VulkanCommands>();
        value->owner = owner;
        value->commandPool = pool;
        value->qType = pool->qType;
        value->pooledCommand = cmd;
        value->pooledQueries = std::move(queries);
        value->token = std::make_shared<int>();
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->objects.push_back(value->token);
        pool->liveLists.push_back(value.get());
        return std::unique_ptr<CommandList>(std::move(value));
    }
    Result<void> reset() override {
        if (auto ok = owner->pollQueue(pool->qType); !ok) return ok;
        std::scoped_lock lock(pool->mutex);
        if (pool->openCount > 0) {
            return fail(ErrorCode::InvalidState, "Command pool cannot reset while a command list is recording");
        }
        if (pool->lastSubmitted > owner->queues[queueIndex(pool->qType)].completed.load(std::memory_order_acquire)) {
            return fail(ErrorCode::InvalidState, "Command pool has in-flight submissions");
        }
        if (!pool->retiredBuffers.empty()) {
            owner->native->FreeCommandBuffers(
                owner->native->handle, pool->handle,
                static_cast<std::uint32_t>(pool->retiredBuffers.size()), pool->retiredBuffers.data());
            pool->retiredBuffers.clear();
        }
        if (auto ok = owner->accept(owner->native->ResetCommandPool(owner->native->handle, pool->handle, 0), "vkResetCommandPool"); !ok) {
            return ok;
        }
        for (VulkanCommands* cmd : pool->liveLists) {
            cmd->record.reset();
            cmd->recordedEpoch = 0;
        }
        pool->lastSubmitted = 0;
        ++pool->epoch;
        return {};
    }
    Result<void> bindRecordingThread() override {
        std::scoped_lock lock(pool->mutex);
        if (auto ok = owner->checkAliveForWorker(); !ok) return ok;
        if (!pool->ownerAlive) return fail(ErrorCode::InvalidState, "Owning CommandPool has been destroyed");
        if (pool->openCount > 0) {
            return fail(ErrorCode::InvalidState, "Command pool cannot rebind recording thread while a command list is recording");
        }
        if (pool->lastSubmitted > owner->queues[queueIndex(pool->qType)].completed.load(std::memory_order_acquire)) {
            return fail(ErrorCode::InvalidState, "Command pool cannot rebind recording thread while submissions are in flight");
        }
        pool->recordingThread.store(std::this_thread::get_id(), std::memory_order_release);
        return {};
    }
};
struct VulkanQueue final : CommandQueue {
    std::shared_ptr<State> owner;
    QueueType qType{QueueType::Graphics};
    QueueType type() const override { return qType; }
    Result<Fence> submit(CommandList& value) override {
        CommandList* single = &value;
        return submit(std::span<CommandList* const>{&single, 1});
    }
    Result<Fence> submit(std::span<CommandList* const> lists) override {
        SubmitDesc desc{};
        desc.commandLists = lists;
        return submit(desc);
    }
    Result<Fence> submit(const SubmitDesc& desc) override {
        if (auto ok = owner->pollQueue(qType); !ok) return std::unexpected(ok.error());
        if (desc.commandLists.empty()) return fail(ErrorCode::InvalidArgument, "Batch submit requires at least one command list");
        const std::size_t qi = queueIndex(qType);
        auto& qSlot = owner->queues[qi];
        if (qSlot.last == UINT64_MAX) return fail(ErrorCode::InvalidState, "Fence sequence exhausted");
        std::vector<VulkanCommands*> validated;
        std::vector<std::shared_ptr<Recording>> records;
        std::vector<VkCommandBuffer> nativeBuffers;
        validated.reserve(desc.commandLists.size());
        records.reserve(desc.commandLists.size());
        nativeBuffers.reserve(desc.commandLists.size());
        std::unordered_set<VulkanCommands*> seenLists;
        std::unordered_set<Allocation*> seenRead;
        std::unordered_set<Allocation*> seenMutated;
        for (CommandList* item : desc.commandLists) {
            if (!item) return fail(ErrorCode::InvalidArgument, "Null command list in batch submit");
            auto* commands = dynamic_cast<VulkanCommands*>(item);
            if (!commands || commands->owner != owner) return fail(ErrorCode::WrongDevice, "Command list belongs to another device");
            if (commands->qType != qType) return fail(ErrorCode::InvalidArgument, "Command list queue type does not match target queue");
            if (!seenLists.insert(commands).second) return fail(ErrorCode::InvalidArgument, "Duplicate command list in batch submit");
            if (commands->commandPool) {
                std::scoped_lock lock(commands->commandPool->mutex);
                if (!commands->commandPool->ownerAlive) return fail(ErrorCode::InvalidState, "Owning CommandPool has been destroyed");
                if (commands->commandPool->openCount > 0) return fail(ErrorCode::InvalidState, "Command pool has a command list currently recording");
                if (commands->recordedEpoch != commands->commandPool->epoch) return fail(ErrorCode::InvalidState, "Pooled command list was reset and must be re-recorded");
            }
            auto record = commands->record;
            if (!record || !record->ended || record->open || record->submitted) return fail(ErrorCode::InvalidState, "Command list must be ended and unsubmitted");
            for (const auto& use : record->uses) {
                if (use.version != use.allocation->version || use.allocation->version == UINT64_MAX || !currentBackbuffer(*use.allocation))
                    return fail(ErrorCode::InvalidState, "Stale command resource version");
                Allocation* key = use.allocation.get();
                if (use.mutated) {
                    if (seenMutated.contains(key) || seenRead.contains(key))
                        return fail(ErrorCode::InvalidState, "Batch submit has conflicting resource mutations across command lists");
                    seenMutated.insert(key);
                } else {
                    if (seenMutated.contains(key))
                        return fail(ErrorCode::InvalidState, "Batch submit has conflicting resource mutation and read across command lists");
                    seenRead.insert(key);
                }
            }
            validated.push_back(commands);
            nativeBuffers.push_back(record->command);
            records.push_back(std::move(record));
        }
        if ((!desc.waits.empty() || !desc.signals.empty()) && !owner->native->timelineSemaphores) {
            return fail(ErrorCode::Unsupported, "Adapter does not support VK_KHR_timeline_semaphore");
        }
        std::vector<VkSemaphore> waitSems;
        std::vector<std::uint64_t> waitVals;
        std::vector<VkPipelineStageFlags> waitStages;
        std::vector<std::shared_ptr<NativeTimelineSemaphore>> retainedTimelines;
        waitSems.reserve(desc.waits.size());
        waitVals.reserve(desc.waits.size());
        waitStages.reserve(desc.waits.size());
        const VkPipelineStageFlags waitStage = stageScope(qType, false);
        for (const auto& w : desc.waits) {
            if (!w.fence) return fail(ErrorCode::InvalidArgument, "Null timeline fence in submit waits");
            auto vkFence = std::dynamic_pointer_cast<VulkanTimelineFence>(w.fence);
            if (!vkFence || vkFence->owner != owner) return fail(ErrorCode::WrongDevice, "Timeline fence belongs to another device");
            waitSems.push_back(vkFence->sem->handle);
            waitVals.push_back(w.value);
            waitStages.push_back(waitStage);
            retainedTimelines.push_back(vkFence->sem);
        }
        std::vector<VkSemaphore> signalSems;
        std::vector<std::uint64_t> signalVals;
        std::vector<std::pair<std::shared_ptr<NativeTimelineSemaphore>, std::uint64_t>> signaledTimelines;
        signalSems.reserve(desc.signals.size());
        signalVals.reserve(desc.signals.size());
        signaledTimelines.reserve(desc.signals.size());
        std::unordered_map<NativeTimelineSemaphore*, std::uint64_t> pendingSignalMax;
        for (const auto& s : desc.signals) {
            if (!s.fence) return fail(ErrorCode::InvalidArgument, "Null timeline fence in submit signals");
            auto vkFence = std::dynamic_pointer_cast<VulkanTimelineFence>(s.fence);
            if (!vkFence || vkFence->owner != owner) return fail(ErrorCode::WrongDevice, "Timeline fence belongs to another device");
            auto currentVal = vkFence->completedValue();
            if (!currentVal) return std::unexpected(currentVal.error());
            const auto it = pendingSignalMax.find(vkFence->sem.get());
            const std::uint64_t prev = it != pendingSignalMax.end()
                ? it->second
                : std::max(vkFence->sem->lastSignaled, *currentVal);
            if (s.value <= prev || s.value == UINT64_MAX) {
                return fail(ErrorCode::InvalidArgument, "TimelineFence signal value must strictly advance");
            }
            pendingSignalMax[vkFence->sem.get()] = s.value;
            signalSems.push_back(vkFence->sem->handle);
            signalVals.push_back(s.value);
            retainedTimelines.push_back(vkFence->sem);
            signaledTimelines.emplace_back(vkFence->sem, s.value);
        }
        // Allocate the retention node before native submission; no throwing work follows success.
        auto& pending = qSlot.pending.emplace_back(Pending{
            {}, qSlot.last + 1, std::move(records), std::move(retainedTimelines), signaledTimelines});
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        auto r = vulkan::acquireOutput(pending.fence, owner->native->CreateFence, owner->native->handle, &fence, nullptr);
        if (r != VK_SUCCESS) {
            qSlot.pending.pop_back();
            auto status = owner->accept(r, "vkCreateFence");
            return std::unexpected(status.error());
        }
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        info.commandBufferCount = static_cast<std::uint32_t>(nativeBuffers.size());
        info.pCommandBuffers = nativeBuffers.data();
        VkTimelineSemaphoreSubmitInfoKHR timelineInfo{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR};
        if (!waitSems.empty() || !signalSems.empty()) {
            info.waitSemaphoreCount = static_cast<std::uint32_t>(waitSems.size());
            info.pWaitSemaphores = waitSems.data();
            info.pWaitDstStageMask = waitStages.data();
            info.signalSemaphoreCount = static_cast<std::uint32_t>(signalSems.size());
            info.pSignalSemaphores = signalSems.data();
            timelineInfo.waitSemaphoreValueCount = static_cast<std::uint32_t>(waitVals.size());
            timelineInfo.pWaitSemaphoreValues = waitVals.data();
            timelineInfo.signalSemaphoreValueCount = static_cast<std::uint32_t>(signalVals.size());
            timelineInfo.pSignalSemaphoreValues = signalVals.data();
            info.pNext = &timelineInfo;
        }
        const auto submittedAt = std::chrono::steady_clock::now();
        r = owner->native->QueueSubmit(owner->native->queues[qi], 1, &info, pending.fence);
        if (r != VK_SUCCESS) {
            owner->native->DestroyFence(owner->native->handle, pending.fence, nullptr); qSlot.pending.pop_back();
            if (auto ok = owner->accept(r, "vkQueueSubmit"); !ok) return std::unexpected(ok.error());
        }
        ++qSlot.last;
        for (const auto& [sem, val] : signaledTimelines) sem->lastSignaled = val;
        std::unordered_set<Allocation*> versionBumped;
        for (std::size_t i = 0; i < validated.size(); ++i) {
            auto* commands = validated[i];
            auto& record = pending.recordings[i];
            record->submitted = qSlot.last;
            record->submittedAt = submittedAt;
            if (commands->commandPool) {
                std::scoped_lock lock(commands->commandPool->mutex);
                commands->commandPool->lastSubmitted = std::max(commands->commandPool->lastSubmitted, qSlot.last);
            }
            for (auto& use : record->uses) {
                if (use.mutated) {
                    use.allocation->state = use.shadow;
                    use.allocation->imageInitialized = use.imageInitialized;
                    use.allocation->contentsInitialized = use.contentsInitialized;
                }
                use.allocation->lastUse[qi] = qSlot.last;
                if (versionBumped.insert(use.allocation.get()).second) ++use.allocation->version;
            }
        }
        if (owner->profiler) owner->profiler->recordSubmit();
        return Fence{qSlot.last, qSlot.id};
    }
    Result<void> wait(Fence target, std::chrono::milliseconds timeout) override {
        if (auto ok = owner->check(); !ok) return ok;
        if (timeout.count() < 0 || timeout.count() > 4294967294ll) return fail(ErrorCode::InvalidArgument, "Fence timeout outside finite range");
        auto& qSlot = owner->queues[queueIndex(qType)];
        if (target.device != qSlot.id) return fail(ErrorCode::WrongDevice, "Fence belongs to another generation or queue");
        if (!target.value || target.value > qSlot.last) return fail(ErrorCode::InvalidArgument, "Fence value was not submitted");
        const bool track = owner->profiler && owner->profiler->isEnabled();
        const auto t0 = track ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        if (auto ok = owner->pollQueue(qType); !ok) return ok;
        if (target.value <= qSlot.completed.load(std::memory_order_acquire)) {
            if (track) {
                const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
                owner->profiler->recordQueueWait(ns > 0 ? static_cast<std::uint64_t>(ns) : 0u);
            }
            return {};
        }
        auto found = std::find_if(qSlot.pending.begin(), qSlot.pending.end(), [&](auto& p) { return p.serial == target.value; });
        if (found == qSlot.pending.end()) return fail(ErrorCode::InvalidState, "Pending fence missing");
        if (auto ok = owner->accept(owner->native->WaitForFences(owner->native->handle, 1, &found->fence, VK_TRUE,
            static_cast<std::uint64_t>(timeout.count()) * 1000000ull), "vkWaitForFences"); !ok) return ok;
        auto polled = owner->pollQueue(qType);
        if (polled && track) {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
            owner->profiler->recordQueueWait(ns > 0 ? static_cast<std::uint64_t>(ns) : 0u);
        }
        return polled;
    }
    Result<void> waitIdle() override {
        if (auto ok = owner->check(); !ok) return ok;
        if (auto ok = owner->accept(owner->native->QueueWaitIdle(owner->native->queues[queueIndex(qType)]), "vkQueueWaitIdle"); !ok) return ok;
        return owner->pollQueue(qType);
    }
    Result<std::uint64_t> completedValue() override {
        if (auto ok = owner->pollQueue(qType); !ok) return std::unexpected(ok.error());
        return owner->queues[queueIndex(qType)].completed.load(std::memory_order_acquire);
    }
};
#include "Presentation.hpp"
struct VulkanDevice final : Device {
    std::shared_ptr<State> owner;
    DeviceCaps capabilities;
    DeviceDesc description;
    bool recoverySucceeded{};
    std::array<VulkanQueue, 3> queues;
    void attachOwner(std::shared_ptr<State> state) {
        owner = std::move(state);
        owner->profiler = profilingTracker();
    }
    ~VulkanDevice() override { owner->retire(); }
    const DeviceCaps& caps() const override { return capabilities; }
    Result<FormatSupport> queryFormatSupport(Format format) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        const auto blockInfo = queryFormatBlockInfo(format);
        const VkFormat vkFormat = textureFormat(format);
        if (!blockInfo || vkFormat == VK_FORMAT_UNDEFINED) {
            return fail(ErrorCode::InvalidArgument, "Unknown texture format");
        }
        FormatSupport result{};
        result.format = format;
        if (blockInfo->compressed && !owner->compressionFeatureEnabled(format)) {
            result.sampleCounts = 0u;
            return result;
        }
        VkFormatProperties props{};
        owner->native->instance->GetPhysicalDeviceFormatProperties(owner->physical, vkFormat, &props);
        const auto features = props.optimalTilingFeatures;
        const bool isDepth = format == Format::D32Float;
        const bool isCompressed = blockInfo->compressed;
        result.copySource = (features & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0;
        result.copyDestination = (features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0;
        result.renderTarget = !isDepth && !isCompressed && (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
        result.blendable = result.renderTarget && (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0;
        result.depthStencil = isDepth && (features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0 && capabilities.depthAttachment;
        result.sampled = !isDepth && (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
        result.linearFilter = result.sampled && (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
        result.texture2D = result.copySource && result.copyDestination && (isDepth ? result.depthStencil : result.sampled);
        result.partialRegionCopy = result.texture2D && !isDepth;
        result.storage = !isDepth && !isCompressed && (features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
        result.sampleCounts = result.texture2D ? 1u : 0u;
        if (result.texture2D && (isDepth ? result.depthStencil : result.renderTarget)) {
            const VkImageUsageFlags msaaUsage = isDepth
                ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                : (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            VkImageFormatProperties msaaProps{};
            if (owner->native->instance->GetPhysicalDeviceImageFormatProperties(
                    owner->physical, vkFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, msaaUsage, 0, &msaaProps) == VK_SUCCESS) {
                const VkSampleCountFlags fbMask = isDepth
                    ? owner->limits.framebufferDepthSampleCounts
                    : owner->limits.framebufferColorSampleCounts;
                const VkSampleCountFlags supportedMask = msaaProps.sampleCounts & fbMask;
                for (const std::uint32_t count : {2u, 4u, 8u}) {
                    if ((supportedMask & vulkanSampleCount(count)) != 0u) {
                        result.sampleCounts |= count;
                    }
                }
            }
        }
        return result;
    }
    Result<void> checkStatus() override { return owner->poll(); }
    Result<ResourceStatistics> resourceStatistics() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        ResourceStatistics result;
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
        std::erase_if(owner->memoryHeaps, [](auto& w) { return w.expired(); });
        {
            std::scoped_lock recLock(owner->recordingsMutex);
            std::erase_if(owner->recordings, [](auto& w) { return w.expired(); });
        }
        std::erase_if(owner->pipelines, [](auto& w) { return w.expired(); });
        result.trackedObjects = owner->objects.size();
        for (auto& w : owner->allocations) if (auto a = w.lock(); a && !a->heap) { ++result.uniqueResources; result.resourceAllocationBytes += a->bytes; }
        for (auto& w : owner->memoryHeaps) if (auto h = w.lock()) { ++result.uniqueResources; result.resourceAllocationBytes += h->desc.size; }
        for (const auto& q : owner->queues) {
            result.pendingSubmissions += q.pending.size();
            for (const auto& p : q.pending) {
                result.pendingObjectReferences += p.timelineSemaphores.size();
                for (const auto& rec : p.recordings) {
                    result.pendingObjectReferences += 1 + rec->uses.size() + rec->passes.size() + rec->pipelines.size() + (rec->queryHandle() ? 1 : 0);
                }
            }
        }
        return result;
    }
    Result<MemoryBudget> queryMemoryBudget(MemorySegment) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return fail(ErrorCode::Unsupported, "Vulkan heap sizes do not provide the OS process budget contract");
    }
    Result<std::unique_ptr<Device>> recover() override {
        if (std::this_thread::get_id() != owner->thread) return fail(ErrorCode::InvalidState, "Recovery requires the creating thread");
        if (recoverySucceeded) return fail(ErrorCode::InvalidState, "This generation has already been recovered");
        auto status = owner->poll();
        if (status || status.error().code != ErrorCode::DeviceLost || !owner->lost)
            return fail(ErrorCode::InvalidState, "Recovery requires an observed device loss");
        // Retire once, before any new native creation. A failed attempt stays retryable;
        // old public children keep only inert descriptions, never the new generation.
        owner->retire();
        try {
            auto fresh = gfx::createDevice(description);
            if (fresh) recoverySucceeded = true;
            return fresh;
        } catch (const std::bad_alloc&) { return fail(ErrorCode::OutOfMemory, "Recovery allocation failed"); }
    }
    Result<void> removeForTesting() override {
        if (auto ok = owner->check(); !ok) return ok;
        return fail(ErrorCode::Unsupported, "Vulkan loss injection unsupported");
    }
    Result<BufferPtr> createBuffer(BufferDesc desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (!desc.size || desc.size > SIZE_MAX || desc.size > (1ull << 40) || (desc.memory != Memory::Device && desc.memory != Memory::Upload && desc.memory != Memory::Readback) ||
            (desc.storage && desc.memory != Memory::Device))
            return fail(ErrorCode::InvalidArgument, "Invalid Vulkan buffer description");
        auto a = std::make_shared<Allocation>(); a->native = owner->native; a->desc = desc;
        a->state = desc.memory == Memory::Upload ? ResourceState::CopySource : desc.memory == Memory::Readback ? ResourceState::CopyDestination : ResourceState::Common;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size = desc.size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
            (desc.storage ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : 0u);
        if (owner->uniqueFamilies.size() > 1) {
            info.sharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = static_cast<std::uint32_t>(owner->uniqueFamilies.size());
            info.pQueueFamilyIndices = owner->uniqueFamilies.data();
        } else {
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        if (auto ok = owner->accept(vulkan::acquireOutput(a->buffer, owner->native->CreateBuffer, owner->native->handle, &info, nullptr), "vkCreateBuffer"); !ok) return std::unexpected(ok.error());
        VkMemoryRequirements req{}; owner->native->GetBufferMemoryRequirements(owner->native->handle, a->buffer, &req);
        const VkMemoryPropertyFlags required = desc.memory == Memory::Device ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        std::optional<std::uint32_t> type;
        for (std::uint32_t i = 0; i < owner->memory.memoryTypeCount; ++i) {
            auto flags = owner->memory.memoryTypes[i].propertyFlags;
            if (!(req.memoryTypeBits & (1u << i)) || (flags & required) != required) continue;
            if (!type) type = i;
            if (desc.memory != Memory::Device && (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { type = i; break; }
        }
        if (!type) return fail(ErrorCode::Unsupported, "No compatible Vulkan buffer memory type");
        a->bytes = req.size; a->coherent = (owner->memory.memoryTypes[*type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocate.allocationSize = req.size; allocate.memoryTypeIndex = *type;
        if (auto ok = owner->accept(vulkan::acquireOutput(a->memory, owner->native->AllocateMemory, owner->native->handle, &allocate, nullptr), "vkAllocateMemory"); !ok) return std::unexpected(ok.error());
        if (auto ok = owner->accept(owner->native->BindBufferMemory(owner->native->handle, a->buffer, a->memory, 0), "vkBindBufferMemory"); !ok) return std::unexpected(ok.error());
        auto value = std::make_shared<VulkanBuffer>(); value->owner = owner; value->allocation = a; value->token = std::make_shared<int>();
        std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->allocations.push_back(a); owner->objects.push_back(value->token); return BufferPtr(value);
    }
    Result<TexturePtr> createTexture(TextureDesc desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (auto ok = validateTextureDescription(desc); !ok) return std::unexpected(ok.error());
        if (!owner->compressionFeatureEnabled(desc.format)) {
            return fail(ErrorCode::Unsupported, "Vulkan adapter does not support requested compressed texture format family");
        }
        if ((desc.renderTarget || desc.depthStencil) && (desc.width > owner->maxFramebufferWidth || desc.height > owner->maxFramebufferHeight))
            return fail(ErrorCode::Unsupported, "Color attachment exceeds adapter framebuffer extent limits");
        const auto format = textureFormat(desc.format);
        if (desc.storage) {
            VkFormatProperties fmtProps{};
            owner->native->instance->GetPhysicalDeviceFormatProperties(owner->physical, format, &fmtProps);
            if (!(fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
                return fail(ErrorCode::Unsupported, "Adapter format lacks Vulkan storage image support");
            }
        }
        const VkImageType imageType = vulkanImageType(desc.dimension);
        const VkImageCreateFlags createFlags = desc.dimension == TextureDimension::TextureCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u;
        const std::uint32_t sliceHeight = desc.dimension == TextureDimension::Texture1D ? 1u : desc.height;
        const std::uint32_t imageDepth = desc.dimension == TextureDimension::Texture3D ? desc.depth : 1u;
        const VkSampleCountFlagBits sampleBit = vulkanSampleCount(desc.sampleCount);
        const VkImageUsageFlags usage = desc.sampleCount > 1u
            ? (desc.depthStencil ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
            : (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               (desc.depthStencil ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_SAMPLED_BIT | (desc.renderTarget ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0u) | (desc.storage ? VK_IMAGE_USAGE_STORAGE_BIT : 0u)));
        VkImageFormatProperties properties{};
        const auto r = owner->native->instance->GetPhysicalDeviceImageFormatProperties(owner->physical, format,
            imageType, VK_IMAGE_TILING_OPTIMAL, usage, createFlags, &properties);
        if (r == VK_ERROR_FORMAT_NOT_SUPPORTED) return fail(ErrorCode::Unsupported, "Vulkan color image format unsupported", r);
        if (auto ok = owner->accept(r, "vkGetPhysicalDeviceImageFormatProperties"); !ok) return std::unexpected(ok.error());
        const VkSampleCountFlags fbMask = desc.depthStencil
            ? owner->limits.framebufferDepthSampleCounts
            : owner->limits.framebufferColorSampleCounts;
        if (desc.width > properties.maxExtent.width || sliceHeight > properties.maxExtent.height ||
            imageDepth > properties.maxExtent.depth || desc.mipLevels > properties.maxMipLevels ||
            desc.arrayLayers > properties.maxArrayLayers ||
            !(properties.sampleCounts & sampleBit) ||
            (desc.sampleCount > 1u && !(fbMask & sampleBit)))
            return fail(ErrorCode::Unsupported, "Vulkan image exceeds adapter format, dimension, mip, array, or MSAA limits");
        auto a = std::make_shared<Allocation>(); a->native = owner->native; a->textureDesc = desc;
        if (desc.sampleCount == 1u) {
            const auto fp = computeTextureRegionFootprint(desc.format, desc.width, sliceHeight);
            if (!fp) return std::unexpected(fp.error());
            a->footprint = {fp->size * imageDepth, fp->rowPitch, fp->rowBytes, fp->height};
        } else {
            a->footprint = {};
        }
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.flags = createFlags;
        info.imageType = imageType; info.format = format; info.extent = {desc.width, sliceHeight, imageDepth};
        info.mipLevels = desc.mipLevels; info.arrayLayers = desc.arrayLayers; info.samples = sampleBit; info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        if (owner->uniqueFamilies.size() > 1) {
            info.sharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = static_cast<std::uint32_t>(owner->uniqueFamilies.size());
            info.pQueueFamilyIndices = owner->uniqueFamilies.data();
        } else {
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (auto ok = owner->accept(vulkan::acquireOutput(a->image, owner->native->CreateImage, owner->native->handle, &info, nullptr), "vkCreateImage"); !ok) return std::unexpected(ok.error());
        VkMemoryRequirements req{}; owner->native->GetImageMemoryRequirements(owner->native->handle, a->image, &req);
        std::optional<std::uint32_t> type;
        for (std::uint32_t n = 0; n < owner->memory.memoryTypeCount; ++n)
            if ((req.memoryTypeBits & (1u << n)) && (owner->memory.memoryTypes[n].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { type = n; break; }
        if (!type) return fail(ErrorCode::Unsupported, "No device-local Vulkan image memory type");
        a->bytes = req.size;
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocate.allocationSize = req.size; allocate.memoryTypeIndex = *type;
        if (auto ok = owner->accept(vulkan::acquireOutput(a->memory, owner->native->AllocateMemory, owner->native->handle, &allocate, nullptr), "vkAllocateMemory image"); !ok) return std::unexpected(ok.error());
        if (auto ok = owner->accept(owner->native->BindImageMemory(owner->native->handle, a->image, a->memory, 0), "vkBindImageMemory"); !ok) return std::unexpected(ok.error());
        {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = a->image; view.viewType = vulkanImageViewType(desc.dimension); view.format = format;
            view.subresourceRange = {imageAspect(*a), 0, desc.mipLevels, 0, desc.arrayLayers};
            if (auto ok = owner->accept(vulkan::acquireOutput(a->view, owner->native->CreateImageView, owner->native->handle, &view, nullptr), "vkCreateImageView"); !ok) return std::unexpected(ok.error());
        }
        if ((desc.renderTarget || desc.depthStencil) && desc.dimension == TextureDimension::Texture2D && desc.mipLevels > 1u) {
            VkImageViewCreateInfo attView{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            attView.image = a->image; attView.viewType = VK_IMAGE_VIEW_TYPE_2D; attView.format = format;
            attView.subresourceRange = {imageAspect(*a), 0, 1, 0, 1};
            if (auto ok = owner->accept(vulkan::acquireOutput(a->attachmentView, owner->native->CreateImageView, owner->native->handle, &attView, nullptr), "vkCreateImageView attachment"); !ok) return std::unexpected(ok.error());
        }
        auto value = std::make_shared<VulkanTexture>(); value->owner = owner; value->allocation = a; value->token = std::make_shared<int>();
        std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->allocations.push_back(a); owner->objects.push_back(value->token); return TexturePtr(value);
    }
    VkBufferCreateInfo buildBufferCreateInfo(const BufferDesc& desc) const {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = desc.size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                     (desc.storage ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : 0u);
        if (owner->uniqueFamilies.size() > 1) {
            info.sharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = static_cast<std::uint32_t>(owner->uniqueFamilies.size());
            info.pQueueFamilyIndices = owner->uniqueFamilies.data();
        } else {
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        return info;
    }
    Result<VkImageCreateInfo> buildValidatedImageCreateInfo(const TextureDesc& desc, VkImageCreateFlags extraFlags) {
        if (auto ok = validateTextureDescription(desc); !ok) return std::unexpected(ok.error());
        if (!owner->compressionFeatureEnabled(desc.format)) {
            return fail(ErrorCode::Unsupported, "Vulkan adapter does not support requested compressed texture format family");
        }
        if ((desc.renderTarget || desc.depthStencil) && (desc.width > owner->maxFramebufferWidth || desc.height > owner->maxFramebufferHeight))
            return fail(ErrorCode::Unsupported, "Color attachment exceeds adapter framebuffer extent limits");
        const auto format = textureFormat(desc.format);
        if (desc.storage) {
            VkFormatProperties fmtProps{};
            owner->native->instance->GetPhysicalDeviceFormatProperties(owner->physical, format, &fmtProps);
            if (!(fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
                return fail(ErrorCode::Unsupported, "Adapter format lacks Vulkan storage image support");
            }
        }
        const VkImageType imageType = vulkanImageType(desc.dimension);
        const VkImageCreateFlags createFlags =
            (desc.dimension == TextureDimension::TextureCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u) | extraFlags;
        const std::uint32_t sliceHeight = desc.dimension == TextureDimension::Texture1D ? 1u : desc.height;
        const std::uint32_t imageDepth = desc.dimension == TextureDimension::Texture3D ? desc.depth : 1u;
        const VkSampleCountFlagBits sampleBit = vulkanSampleCount(desc.sampleCount);
        const VkImageUsageFlags usage = desc.sampleCount > 1u
            ? (desc.depthStencil ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
            : (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               (desc.depthStencil ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_SAMPLED_BIT | (desc.renderTarget ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0u) | (desc.storage ? VK_IMAGE_USAGE_STORAGE_BIT : 0u)));
        VkImageFormatProperties properties{};
        const auto r = owner->native->instance->GetPhysicalDeviceImageFormatProperties(owner->physical, format,
            imageType, VK_IMAGE_TILING_OPTIMAL, usage, createFlags, &properties);
        if (r == VK_ERROR_FORMAT_NOT_SUPPORTED) return fail(ErrorCode::Unsupported, "Vulkan image format or flags unsupported", r);
        if (auto ok = owner->accept(r, "vkGetPhysicalDeviceImageFormatProperties"); !ok) return std::unexpected(ok.error());
        const VkSampleCountFlags fbMask = desc.depthStencil
            ? owner->limits.framebufferDepthSampleCounts
            : owner->limits.framebufferColorSampleCounts;
        if (desc.width > properties.maxExtent.width || sliceHeight > properties.maxExtent.height ||
            imageDepth > properties.maxExtent.depth || desc.mipLevels > properties.maxMipLevels ||
            desc.arrayLayers > properties.maxArrayLayers ||
            !(properties.sampleCounts & sampleBit) ||
            (desc.sampleCount > 1u && !(fbMask & sampleBit)))
            return fail(ErrorCode::Unsupported, "Vulkan image exceeds adapter format, dimension, mip, array, or MSAA limits");
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.flags = createFlags;
        info.imageType = imageType;
        info.format = format;
        info.extent = {desc.width, sliceHeight, imageDepth};
        info.mipLevels = desc.mipLevels;
        info.arrayLayers = desc.arrayLayers;
        info.samples = sampleBit;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        if (owner->uniqueFamilies.size() > 1) {
            info.sharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = static_cast<std::uint32_t>(owner->uniqueFamilies.size());
            info.pQueueFamilyIndices = owner->uniqueFamilies.data();
        } else {
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return info;
    }
    Result<ResourceMemoryRequirements> queryBufferMemoryRequirements(BufferDesc desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (!desc.size || desc.size > SIZE_MAX || desc.memory != Memory::Device) {
            return fail(ErrorCode::InvalidArgument, "Placed buffer requires non-empty Device memory");
        }
        const auto info = buildBufferCreateInfo(desc);
        VkBuffer probe{};
        if (auto ok = owner->accept(vulkan::acquireOutput(probe, owner->native->CreateBuffer, owner->native->handle, &info, nullptr), "vkCreateBuffer(probe)"); !ok)
            return std::unexpected(ok.error());
        VkMemoryRequirements req{};
        owner->native->GetBufferMemoryRequirements(owner->native->handle, probe, &req);
        owner->native->DestroyBuffer(owner->native->handle, probe, nullptr);
        return ResourceMemoryRequirements{req.size, std::max<std::uint64_t>(1u, req.alignment), MemoryHeapType::Buffers};
    }
    Result<ResourceMemoryRequirements> queryTextureMemoryRequirements(TextureDesc desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        auto info = buildValidatedImageCreateInfo(desc, VK_IMAGE_CREATE_ALIAS_BIT);
        if (!info) return std::unexpected(info.error());
        VkImage probe{};
        if (auto ok = owner->accept(vulkan::acquireOutput(probe, owner->native->CreateImage, owner->native->handle, &*info, nullptr), "vkCreateImage(probe)"); !ok)
            return std::unexpected(ok.error());
        VkMemoryRequirements req{};
        owner->native->GetImageMemoryRequirements(owner->native->handle, probe, &req);
        owner->native->DestroyImage(owner->native->handle, probe, nullptr);
        const MemoryHeapType heapType = (desc.renderTarget || desc.depthStencil)
            ? MemoryHeapType::RenderTargets
            : MemoryHeapType::Textures;
        return ResourceMemoryRequirements{req.size, std::max<std::uint64_t>(1u, req.alignment), heapType};
    }
    Result<MemoryHeapPtr> createMemoryHeap(MemoryHeapDesc desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (auto ok = validateMemoryHeapDesc(desc); !ok) return std::unexpected(ok.error());
        std::uint32_t typeBits = ~0u;
        if (desc.type == MemoryHeapType::Buffers) {
            const auto bInfo = buildBufferCreateInfo(BufferDesc{65536u, Memory::Device, true});
            VkBuffer probe{};
            if (auto ok = owner->accept(vulkan::acquireOutput(probe, owner->native->CreateBuffer, owner->native->handle, &bInfo, nullptr), "vkCreateBuffer(heap probe)"); !ok)
                return std::unexpected(ok.error());
            VkMemoryRequirements req{};
            owner->native->GetBufferMemoryRequirements(owner->native->handle, probe, &req);
            owner->native->DestroyBuffer(owner->native->handle, probe, nullptr);
            typeBits = req.memoryTypeBits;
        } else {
            TextureDesc probeDesc{64u, 64u, Format::RGBA8UNorm, desc.type == MemoryHeapType::RenderTargets, false, false};
            auto iInfo = buildValidatedImageCreateInfo(probeDesc, VK_IMAGE_CREATE_ALIAS_BIT);
            if (!iInfo) return std::unexpected(iInfo.error());
            VkImage probe{};
            if (auto ok = owner->accept(vulkan::acquireOutput(probe, owner->native->CreateImage, owner->native->handle, &*iInfo, nullptr), "vkCreateImage(heap probe)"); !ok)
                return std::unexpected(ok.error());
            VkMemoryRequirements req{};
            owner->native->GetImageMemoryRequirements(owner->native->handle, probe, &req);
            owner->native->DestroyImage(owner->native->handle, probe, nullptr);
            typeBits = req.memoryTypeBits;
        }
        std::optional<std::uint32_t> memoryType;
        for (std::uint32_t n = 0; n < owner->memory.memoryTypeCount; ++n) {
            if ((typeBits & (1u << n)) && (owner->memory.memoryTypes[n].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memoryType = n;
                break;
            }
        }
        if (!memoryType) return fail(ErrorCode::Unsupported, "No compatible device-local Vulkan memory type for MemoryHeap");
        auto nativeHeap = std::make_shared<NativeMemoryHeap>();
        nativeHeap->native = owner->native;
        nativeHeap->desc = desc;
        nativeHeap->memoryTypeIndex = *memoryType;
        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = desc.size;
        allocInfo.memoryTypeIndex = *memoryType;
        if (auto ok = owner->accept(vulkan::acquireOutput(nativeHeap->memory, owner->native->AllocateMemory, owner->native->handle, &allocInfo, nullptr), "vkAllocateMemory(MemoryHeap)"); !ok)
            return std::unexpected(ok.error());
        auto wrapper = std::make_shared<VulkanMemoryHeap>();
        wrapper->owner = owner;
        wrapper->heap = nativeHeap;
        wrapper->token = std::make_shared<int>();
        std::erase_if(owner->memoryHeaps, [](auto& w) { return w.expired(); });
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->memoryHeaps.push_back(nativeHeap);
        owner->objects.push_back(wrapper->token);
        return MemoryHeapPtr(std::move(wrapper));
    }
    Result<BufferPtr> createPlacedBuffer(const MemoryHeapPtr& heap, std::uint64_t heapOffset, BufferDesc desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (!heap) return fail(ErrorCode::InvalidArgument, "Null MemoryHeap");
        auto vkHeap = std::dynamic_pointer_cast<VulkanMemoryHeap>(heap);
        if (!vkHeap || vkHeap->owner != owner) return fail(ErrorCode::WrongDevice, "MemoryHeap belongs to another device");
        if (!desc.size || desc.size > SIZE_MAX || desc.memory != Memory::Device) {
            return fail(ErrorCode::InvalidArgument, "Placed buffer requires non-empty Device memory");
        }
        if (vkHeap->heap->desc.type != MemoryHeapType::Buffers) {
            return fail(ErrorCode::InvalidArgument, "MemoryHeapType mismatch for placed buffer");
        }
        const auto info = buildBufferCreateInfo(desc);
        auto a = std::make_shared<Allocation>();
        a->native = owner->native;
        a->desc = desc;
        a->state = ResourceState::Common;
        if (auto ok = owner->accept(vulkan::acquireOutput(a->buffer, owner->native->CreateBuffer, owner->native->handle, &info, nullptr), "vkCreateBuffer(placed)"); !ok)
            return std::unexpected(ok.error());
        VkMemoryRequirements req{};
        owner->native->GetBufferMemoryRequirements(owner->native->handle, a->buffer, &req);
        const std::uint64_t alignment = std::max<std::uint64_t>(1u, req.alignment);
        if ((req.memoryTypeBits & (1u << vkHeap->heap->memoryTypeIndex)) == 0u) {
            return fail(ErrorCode::Unsupported, "Placed buffer memory type is incompatible with MemoryHeap");
        }
        if (heapOffset % alignment != 0u) {
            return fail(ErrorCode::InvalidArgument, "Placed buffer heapOffset is not aligned");
        }
        if (heapOffset > vkHeap->heap->desc.size || req.size > vkHeap->heap->desc.size - heapOffset) {
            return fail(ErrorCode::InvalidArgument, "Placed buffer exceeds MemoryHeap size");
        }
        if (auto ok = owner->accept(owner->native->BindBufferMemory(owner->native->handle, a->buffer, vkHeap->heap->memory, heapOffset), "vkBindBufferMemory(placed)"); !ok)
            return std::unexpected(ok.error());
        a->heap = vkHeap->heap;
        a->memory = vkHeap->heap->memory;
        a->bytes = 0;
        vkHeap->heap->entries.push_back({a, heapOffset, req.size});
        auto value = std::make_shared<VulkanBuffer>();
        value->owner = owner;
        value->allocation = a;
        value->token = std::make_shared<int>();
        std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->allocations.push_back(a);
        owner->objects.push_back(value->token);
        return BufferPtr(std::move(value));
    }
    Result<TexturePtr> createPlacedTexture(const MemoryHeapPtr& heap, std::uint64_t heapOffset, TextureDesc desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (!heap) return fail(ErrorCode::InvalidArgument, "Null MemoryHeap");
        auto vkHeap = std::dynamic_pointer_cast<VulkanMemoryHeap>(heap);
        if (!vkHeap || vkHeap->owner != owner) return fail(ErrorCode::WrongDevice, "MemoryHeap belongs to another device");
        const MemoryHeapType expectedType = (desc.renderTarget || desc.depthStencil)
            ? MemoryHeapType::RenderTargets
            : MemoryHeapType::Textures;
        if (vkHeap->heap->desc.type != expectedType) {
            return fail(ErrorCode::InvalidArgument, "MemoryHeapType mismatch for placed texture");
        }
        auto info = buildValidatedImageCreateInfo(desc, VK_IMAGE_CREATE_ALIAS_BIT);
        if (!info) return std::unexpected(info.error());
        auto a = std::make_shared<Allocation>();
        a->native = owner->native;
        a->textureDesc = desc;
        const std::uint32_t sliceHeight = desc.dimension == TextureDimension::Texture1D ? 1u : desc.height;
        const std::uint32_t imageDepth = desc.dimension == TextureDimension::Texture3D ? desc.depth : 1u;
        if (desc.sampleCount == 1u) {
            const auto fp = computeTextureRegionFootprint(desc.format, desc.width, sliceHeight);
            if (!fp) return std::unexpected(fp.error());
            a->footprint = {fp->size * imageDepth, fp->rowPitch, fp->rowBytes, fp->height};
        } else {
            a->footprint = {};
        }
        if (auto ok = owner->accept(vulkan::acquireOutput(a->image, owner->native->CreateImage, owner->native->handle, &*info, nullptr), "vkCreateImage(placed)"); !ok)
            return std::unexpected(ok.error());
        VkMemoryRequirements req{};
        owner->native->GetImageMemoryRequirements(owner->native->handle, a->image, &req);
        const std::uint64_t alignment = std::max<std::uint64_t>(1u, req.alignment);
        if ((req.memoryTypeBits & (1u << vkHeap->heap->memoryTypeIndex)) == 0u) {
            return fail(ErrorCode::Unsupported, "Placed texture memory type is incompatible with MemoryHeap");
        }
        if (heapOffset % alignment != 0u) {
            return fail(ErrorCode::InvalidArgument, "Placed texture heapOffset is not aligned");
        }
        if (heapOffset > vkHeap->heap->desc.size || req.size > vkHeap->heap->desc.size - heapOffset) {
            return fail(ErrorCode::InvalidArgument, "Placed texture exceeds MemoryHeap size");
        }
        if (auto ok = owner->accept(owner->native->BindImageMemory(owner->native->handle, a->image, vkHeap->heap->memory, heapOffset), "vkBindImageMemory(placed)"); !ok)
            return std::unexpected(ok.error());
        a->heap = vkHeap->heap;
        a->memory = vkHeap->heap->memory;
        a->bytes = 0;
        {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = a->image;
            view.viewType = vulkanImageViewType(desc.dimension);
            view.format = info->format;
            view.subresourceRange = {imageAspect(*a), 0, desc.mipLevels, 0, desc.arrayLayers};
            if (auto ok = owner->accept(vulkan::acquireOutput(a->view, owner->native->CreateImageView, owner->native->handle, &view, nullptr), "vkCreateImageView(placed)"); !ok)
                return std::unexpected(ok.error());
        }
        if ((desc.renderTarget || desc.depthStencil) && desc.dimension == TextureDimension::Texture2D && desc.mipLevels > 1u) {
            VkImageViewCreateInfo attView{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            attView.image = a->image;
            attView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            attView.format = info->format;
            attView.subresourceRange = {imageAspect(*a), 0, 1, 0, 1};
            if (auto ok = owner->accept(vulkan::acquireOutput(a->attachmentView, owner->native->CreateImageView, owner->native->handle, &attView, nullptr), "vkCreateImageView(placed attachment)"); !ok)
                return std::unexpected(ok.error());
        }
        vkHeap->heap->entries.push_back({a, heapOffset, req.size});
        auto value = std::make_shared<VulkanTexture>();
        value->owner = owner;
        value->allocation = a;
        value->token = std::make_shared<int>();
        std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->allocations.push_back(a);
        owner->objects.push_back(value->token);
        return TexturePtr(std::move(value));
    }
    Result<SparseTextureProperties> querySparseTextureProperties(TextureDesc desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (auto ok = validateTextureDescription(desc); !ok) return std::unexpected(ok.error());
        if (!owner->compressionFeatureEnabled(desc.format)) {
            return fail(ErrorCode::Unsupported, "Vulkan adapter does not support requested compressed texture format family");
        }
        SparseTextureProperties result{};
        if (!capabilities.sparseResidencyTexture2D || desc.sampleCount > 1u ||
            (desc.dimension != TextureDimension::Texture2D &&
             desc.dimension != TextureDimension::Texture2DArray &&
             desc.dimension != TextureDimension::TextureCube)) {
            return result;
        }
        auto info = buildValidatedImageCreateInfo(desc, VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT);
        if (!info) {
            if (info.error().code == ErrorCode::Unsupported) return result;
            return std::unexpected(info.error());
        }
        VkImage probe{};
        if (auto ok = owner->accept(vulkan::acquireOutput(probe, owner->native->CreateImage, owner->native->handle, &*info, nullptr), "vkCreateImage(sparse probe)"); !ok)
            return std::unexpected(ok.error());
        VkMemoryRequirements memReq{};
        owner->native->GetImageMemoryRequirements(owner->native->handle, probe, &memReq);
        std::uint32_t reqCount{};
        owner->native->GetImageSparseMemoryRequirements(owner->native->handle, probe, &reqCount, nullptr);
        std::vector<VkSparseImageMemoryRequirements> sparseReqs(reqCount);
        if (reqCount > 0u) {
            owner->native->GetImageSparseMemoryRequirements(owner->native->handle, probe, &reqCount, sparseReqs.data());
            sparseReqs.resize(reqCount);
        }
        owner->native->DestroyImage(owner->native->handle, probe, nullptr);
        const VkImageAspectFlags targetAspect = desc.depthStencil ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        for (const auto& sr : sparseReqs) {
            if ((sr.formatProperties.aspectMask & targetAspect) == 0u) continue;
            const auto& g = sr.formatProperties.imageGranularity;
            if (g.width == 0u || g.height == 0u || memReq.alignment == 0u) continue;
            result.supported = true;
            result.tileWidth = g.width;
            result.tileHeight = g.height;
            result.tileDepth = g.depth ? g.depth : 1u;
            result.standardTileBytes = static_cast<std::uint32_t>(memReq.alignment);
            result.packedMipStart = sr.imageMipTailFirstLod;
            result.packedMipCount = sr.imageMipTailFirstLod < desc.mipLevels ? (desc.mipLevels - sr.imageMipTailFirstLod) : 0u;
            result.packedMipTiles = sr.imageMipTailSize > 0u
                ? static_cast<std::uint32_t>((sr.imageMipTailSize + memReq.alignment - 1u) / memReq.alignment)
                : 0u;
            result.totalTiles = static_cast<std::uint32_t>((memReq.size + memReq.alignment - 1u) / memReq.alignment);
            break;
        }
        return result;
    }
    Result<TextureViewPtr> createTextureView(const TexturePtr& ptr, TextureViewDesc viewDesc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (!ptr) return fail(ErrorCode::InvalidArgument, "Null texture");
        auto parent = std::dynamic_pointer_cast<VulkanTexture>(ptr);
        if (!parent || parent->owner != owner) return fail(ErrorCode::WrongDevice, "Texture belongs to another device generation");
        if (!currentBackbuffer(*parent->allocation)) return fail(ErrorCode::InvalidState, "Backbuffer is not current or its swapchain/window expired");
        auto resolved = validateTextureView(parent->allocation->textureDesc, viewDesc);
        if (!resolved) return std::unexpected(resolved.error());
        if (resolved->usage == TextureViewUsage::ShaderRead && parent->allocation->swapchain) {
            return fail(ErrorCode::InvalidArgument, "Swapchain backbuffer cannot create a ShaderRead view");
        }
        auto nativeView = std::make_shared<NativeTextureView>();
        nativeView->native = owner->native;
        nativeView->allocation = parent->allocation;
        nativeView->desc = *resolved;
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        info.image = parent->allocation->image;
        info.viewType = vulkanImageViewType(*resolved->dimension);
        info.format = textureFormat(*resolved->format);
        info.subresourceRange = {
            imageAspect(*parent->allocation),
            resolved->baseMipLevel,
            resolved->mipLevelCount,
            resolved->baseArrayLayer,
            resolved->arrayLayerCount
        };
        if (auto ok = owner->accept(vulkan::acquireOutput(nativeView->view, owner->native->CreateImageView, owner->native->handle, &info, nullptr), "vkCreateImageView"); !ok) {
            return std::unexpected(ok.error());
        }
        auto value = std::make_shared<VulkanTextureView>();
        value->owner = owner;
        value->parent = parent;
        value->native = nativeView;
        value->token = std::make_shared<int>();
        std::erase_if(owner->imageViews, [](auto& w) { return w.expired(); });
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->imageViews.push_back(nativeView);
        owner->objects.push_back(value->token);
        return TextureViewPtr(value);
    }
    Result<PipelinePtr> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        const auto targets = desc.renderTargets.value_or(RenderTargetLayout{{desc.colorFormat}, {}});
        const bool hasFragment = !desc.fragment.bytes.empty();
        if (desc.vertex.format != ShaderFormat::SPIRV || (hasFragment && desc.fragment.format != ShaderFormat::SPIRV))
            return fail(ErrorCode::Unsupported, "Vulkan requires explicit SPIRV shader format");
        if (auto ok = validateRenderTargets(targets, desc.depthState); !ok) return std::unexpected(ok.error());
        if (targets.sampleCount > 1u) {
            for (const auto colorFormat : targets.colors) {
                auto support = queryFormatSupport(colorFormat);
                if (!support) return std::unexpected(support.error());
                if ((support->sampleCounts & targets.sampleCount) == 0u)
                    return fail(ErrorCode::Unsupported, "Adapter does not support pipeline color MSAA sample count");
            }
            if (targets.depth) {
                auto support = queryFormatSupport(*targets.depth);
                if (!support) return std::unexpected(support.error());
                if ((support->sampleCounts & targets.sampleCount) == 0u)
                    return fail(ErrorCode::Unsupported, "Adapter does not support pipeline depth MSAA sample count");
            }
        }
        if (!hasFragment && !targets.colors.empty()) return fail(ErrorCode::InvalidArgument, "Color outputs require a fragment shader");
        if (targets.colors.size() > owner->maxColorAttachments) return fail(ErrorCode::Unsupported, "Color outputs exceed adapter limit");
        if (targets.depth) {
            VkFormatProperties properties{}; owner->native->instance->GetPhysicalDeviceFormatProperties(owner->physical, VK_FORMAT_D32_SFLOAT, &properties);
            if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) return fail(ErrorCode::Unsupported, "Adapter lacks D32 depth attachment support");
        }
        if (desc.sampledTexture && (!desc.bindingLayout.entries.empty() || !desc.bindingLayout.samplers.empty())) return fail(ErrorCode::InvalidArgument, "Legacy sampledTexture and explicit layout cannot be combined");
        const auto resourceLayout = desc.sampledTexture ? BindingLayout{{{}}, {{}}} : desc.bindingLayout;
        if (auto ok = validateBindingLayout(resourceLayout); !ok) return std::unexpected(ok.error());
        for (const auto& entry : resourceLayout.entries) {
            if (entry.type == BindingType::StorageBuffer || entry.type == BindingType::StorageTexture2D ||
                entry.visibility == ShaderVisibility::Compute) {
                return fail(ErrorCode::InvalidArgument, "Graphics pipeline layout does not support storage or compute-only bindings");
            }
        }
        for (const auto& sampler : resourceLayout.samplers) {
            if (sampler.visibility == ShaderVisibility::Compute) {
                return fail(ErrorCode::InvalidArgument, "Graphics pipeline layout does not support compute-only samplers");
            }
        }
        if (auto ok = validateVertexLayout(desc.vertexLayout); !ok) return std::unexpected(ok.error());
        if (desc.vertexLayout.attributes.size() > owner->limits.maxVertexInputAttributes) return fail(ErrorCode::Unsupported, "Vertex attributes exceed adapter limit");
        std::map<unsigned, unsigned> vertexVaryings, fragmentVaryings;
        std::vector<unsigned> vertexMinimum, fragmentMinimum(resourceLayout.entries.size());
        auto vertex = vulkan::inspectSpirv(desc.vertex, ShaderStage::Vertex, 0, desc.vertexLayout, &vertexVaryings, resourceLayout, &vertexMinimum); if (!vertex) return std::unexpected(vertex.error());
        Result<std::vector<std::uint32_t>> fragment = std::vector<std::uint32_t>{};
        if (hasFragment) { fragment = vulkan::inspectSpirv(desc.fragment, ShaderStage::Fragment, static_cast<std::uint32_t>(targets.colors.size()), {}, &fragmentVaryings, resourceLayout, &fragmentMinimum); if (!fragment) return std::unexpected(fragment.error()); }
        if (hasFragment && vertexVaryings != fragmentVaryings) return fail(ErrorCode::InvalidArgument, "Vertex/fragment float varying interfaces differ");
        for (auto format : targets.colors) {
            VkFormatProperties properties{};
            owner->native->instance->GetPhysicalDeviceFormatProperties(owner->physical,
                textureFormat(format), &properties);
            const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | (desc.alphaBlend ? VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT : 0);
            if ((properties.optimalTilingFeatures & required) != required) return fail(ErrorCode::Unsupported, "Adapter format lacks attachment/blending support");
        }
        auto p = std::make_shared<NativePipeline>(); p->native = owner->native; p->colors = targets.colors; p->depth = targets.depth; p->sampleCount = targets.sampleCount; p->depthState = desc.depthState;
        p->bindings = resourceLayout; p->sampled = desc.sampledTexture; p->minimumBytes.resize(resourceLayout.entries.size());
        std::array<unsigned, 3> totals{}; std::array<std::array<unsigned, 3>, 2> perStage{};
        auto stages = [](ShaderVisibility v) -> VkShaderStageFlags { return v == ShaderVisibility::All ? VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT : v == ShaderVisibility::Vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT; };
        auto count = [&](unsigned type, unsigned n, ShaderVisibility visibility) {
            totals[type] += n;
            for (unsigned s = 0; s < 2; ++s) if (visibility == ShaderVisibility::All || static_cast<unsigned>(visibility) == s) perStage[s][type] += n;
        };
        for (unsigned i = 0; i < resourceLayout.entries.size(); ++i) {
            const auto& e = resourceLayout.entries[i]; p->descriptorSize += e.count;
            p->minimumBytes[i] = std::max(vertexMinimum[i], fragmentMinimum[i]);
            count(isSampledTextureBinding(e.type) ? 0 : 1, e.count, e.visibility);
        }
        for (const auto& s : resourceLayout.samplers) count(2, 1, s.visibility);
        const auto& limits = owner->limits;
        if (totals[0] > limits.maxDescriptorSetSampledImages || totals[1] > limits.maxDescriptorSetUniformBuffers || totals[2] > limits.maxDescriptorSetSamplers)
            return fail(ErrorCode::Unsupported, "Descriptor set exceeds adapter limits");
        for (unsigned stage = 0; stage < 2; ++stage) { const auto& n = perStage[stage]; if (n[0] > limits.maxPerStageDescriptorSampledImages || n[1] > limits.maxPerStageDescriptorUniformBuffers || n[2] > limits.maxPerStageDescriptorSamplers || n[0] + n[1] + (stage == 1 ? targets.colors.size() : 0) > limits.maxPerStageResources)
            return fail(ErrorCode::Unsupported, "Stage descriptor count exceeds adapter limits"); }
        p->samplers.reserve(resourceLayout.samplers.size());
        for (const auto& s : resourceLayout.samplers) {
            VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            info.magFilter = info.minFilter = s.filter == SamplerFilter::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            info.addressModeU = info.addressModeV = info.addressModeW = s.address == SamplerAddress::Clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
            info.maxLod = VK_LOD_CLAMP_NONE;
            VkSampler sampler{};
            if (auto ok = owner->accept(vulkan::acquireOutput(sampler, owner->native->CreateSampler, owner->native->handle, &info, nullptr), "vkCreateSampler"); !ok) return std::unexpected(ok.error());
            p->samplers.push_back(sampler); p->linear |= s.filter == SamplerFilter::Linear;
        }
        std::vector<VkDescriptorSetLayoutBinding> resources;
        for (unsigned i = 0; i < resourceLayout.entries.size(); ++i) {
            const auto& e = resourceLayout.entries[i]; resources.push_back({i, isSampledTextureBinding(e.type) ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, e.count, stages(e.visibility), nullptr});
        }
        for (unsigned i = 0; i < p->samplers.size(); ++i) resources.push_back({32 + i, VK_DESCRIPTOR_TYPE_SAMPLER, 1, stages(resourceLayout.samplers[i].visibility), &p->samplers[i]});
        if (!resources.empty()) {
            VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; info.bindingCount = static_cast<unsigned>(resources.size()); info.pBindings = resources.data();
            if (auto ok = owner->accept(vulkan::acquireOutput(p->setLayout, owner->native->CreateDescriptorSetLayout, owner->native->handle, &info, nullptr), "vkCreateDescriptorSetLayout"); !ok) return std::unexpected(ok.error());
        }
        std::vector<VkVertexInputBindingDescription> bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
        for (const auto& binding : desc.vertexLayout.buffers) {
            if (binding.slot >= owner->limits.maxVertexInputBindings || binding.stride > owner->limits.maxVertexInputBindingStride) return fail(ErrorCode::Unsupported, "Vertex binding exceeds adapter limit");
            bindings.push_back({binding.slot, binding.stride, binding.rate == VertexInputRate::Instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
            NativePipeline::Input input{binding.slot, binding.stride, 0, 1, binding.rate};
            for (const auto& attribute : desc.vertexLayout.attributes) if (attribute.slot == binding.slot) {
                input.extent = std::max(input.extent, attribute.offset + vertexBytes(attribute.format));
                input.alignment = std::max(input.alignment, attribute.format == VertexFormat::UNorm8x4 ? 1u : 4u);
            }
            p->inputs.push_back(input);
        }
        for (unsigned i = 0; i < desc.vertexLayout.attributes.size(); ++i) {
            const auto& a = desc.vertexLayout.attributes[i];
            if (a.offset > owner->limits.maxVertexInputAttributeOffset) return fail(ErrorCode::Unsupported, "Vertex attribute offset exceeds adapter limit");
            VkFormatProperties properties{}; owner->native->instance->GetPhysicalDeviceFormatProperties(owner->physical, vertexFormat(a.format), &properties);
            if (!(properties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)) return fail(ErrorCode::Unsupported, "Vertex format unsupported by adapter");
            attributes.push_back({i, a.slot, vertexFormat(a.format), a.offset});
        }
        struct Modules {
            std::shared_ptr<NativeDevice> native; VkShaderModule vertex{}, fragment{};
            ~Modules() { if (vertex) native->DestroyShaderModule(native->handle, vertex, nullptr); if (fragment) native->DestroyShaderModule(native->handle, fragment, nullptr); }
        } modules{owner->native};
        auto module = [&](const std::vector<std::uint32_t>& code, VkShaderModule& result) {
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; info.codeSize = code.size() * 4; info.pCode = code.data();
            return owner->accept(vulkan::acquireOutput(result, owner->native->CreateShaderModule, owner->native->handle, &info, nullptr), "vkCreateShaderModule");
        };
        if (auto ok = module(*vertex, modules.vertex); !ok) return std::unexpected(ok.error());
        if (auto ok = hasFragment ? module(*fragment, modules.fragment) : Result<void>{}; !ok) return std::unexpected(ok.error());
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        if (p->setLayout) { layout.setLayoutCount = 1; layout.pSetLayouts = &p->setLayout; }
        if (auto ok = owner->accept(vulkan::acquireOutput(p->layout, owner->native->CreatePipelineLayout, owner->native->handle, &layout, nullptr), "vkCreatePipelineLayout"); !ok) return std::unexpected(ok.error());
        const VkSampleCountFlagBits sampleBit = vulkanSampleCount(targets.sampleCount);
        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> references;
        for (auto format : targets.colors) {
            VkAttachmentDescription a{}; a.format = textureFormat(format);
            a.samples = sampleBit; a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            references.push_back({static_cast<std::uint32_t>(attachments.size()), a.initialLayout}); attachments.push_back(a);
        }
        VkAttachmentReference depthReference{};
        if (targets.depth) {
            VkAttachmentDescription a{}; a.format = VK_FORMAT_D32_SFLOAT; a.samples = sampleBit;
            a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthReference = {static_cast<unsigned>(attachments.size()), a.initialLayout}; attachments.push_back(a);
        }
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<std::uint32_t>(references.size()); subpass.pColorAttachments = references.data();
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; pass.attachmentCount = static_cast<std::uint32_t>(attachments.size()); pass.pAttachments = attachments.data(); pass.subpassCount = 1; pass.pSubpasses = &subpass;
        if (targets.depth) subpass.pDepthStencilAttachment = &depthReference;
        const auto dependencies = passDependencies(); pass.dependencyCount = 2; pass.pDependencies = dependencies.data();
        if (auto ok = owner->accept(vulkan::acquireOutput(p->pass, owner->native->CreateRenderPass, owner->native->handle, &pass, nullptr), "vkCreateRenderPass pipeline"); !ok) return std::unexpected(ok.error());
        VkPipelineShaderStageCreateInfo shaderStages[2]{};
        for (auto& s : shaderStages) { s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; s.pName = "main"; }
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; shaderStages[0].module = modules.vertex; shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; shaderStages[1].module = modules.fragment;
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size()); input.pVertexBindingDescriptions = bindings.data();
        input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()); input.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.frontFace = VK_FRONT_FACE_CLOCKWISE; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; samples.rasterizationSamples = sampleBit;
        std::vector<VkPipelineColorBlendAttachmentState> blending(targets.colors.size());
        for (auto& b : blending) {
            b.blendEnable = desc.alphaBlend; b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; b.colorBlendOp = VK_BLEND_OP_ADD;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; b.alphaBlendOp = VK_BLEND_OP_ADD; b.colorWriteMask = 15;
        }
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blend.attachmentCount = static_cast<std::uint32_t>(blending.size()); blend.pAttachments = blending.data();
        const VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamicStates;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; info.stageCount = hasFragment ? 2 : 1; info.pStages = shaderStages; info.pVertexInputState = &input; info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport; info.pRasterizationState = &raster; info.pMultisampleState = &samples; info.pColorBlendState = &blend; info.pDynamicState = &dynamic;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = desc.depthState.testEnabled; depth.depthWriteEnable = desc.depthState.writeEnabled;
        depth.depthCompareOp = static_cast<VkCompareOp>(desc.depthState.compare); // Both enumerations use Never..Always order.
        if (targets.depth) info.pDepthStencilState = &depth;
        info.layout = p->layout; info.renderPass = p->pass;
        // Pipeline arrays have defined null/partial-success outputs. Keep every
        // non-null handle in RAII cleanup even if the overall call fails.
        if (auto ok = owner->accept(owner->native->CreateGraphicsPipelines(owner->native->handle, {}, 1, &info, nullptr, &p->pipeline), "vkCreateGraphicsPipelines"); !ok) return std::unexpected(ok.error());
        auto value = std::make_shared<VulkanPipeline>(); value->owner = owner; value->native = p; value->token = std::make_shared<int>();
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); }); std::erase_if(owner->pipelines, [](auto& w) { return w.expired(); });
        owner->pipelines.push_back(p); owner->objects.push_back(value->token); return PipelinePtr(value);
    }
    Result<PipelinePtr> createComputePipeline(const ComputePipelineDesc& desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (desc.compute.format != ShaderFormat::SPIRV) return fail(ErrorCode::Unsupported, "Vulkan requires explicit SPIRV shader format");
        if (desc.compute.stage != ShaderStage::Compute || desc.compute.bytes.empty() ||
            !desc.threadgroupSizeX || !desc.threadgroupSizeY || !desc.threadgroupSizeZ) {
            return fail(ErrorCode::InvalidArgument, "Invalid compute pipeline shader or threadgroup dimensions");
        }
        if (auto ok = validateBindingLayout(desc.bindingLayout); !ok) return std::unexpected(ok.error());
        for (const auto& entry : desc.bindingLayout.entries) {
            if (entry.visibility != ShaderVisibility::Compute && entry.visibility != ShaderVisibility::All) {
                return fail(ErrorCode::InvalidArgument, "Compute binding entry must have Compute or All visibility");
            }
        }
        for (const auto& sampler : desc.bindingLayout.samplers) {
            if (sampler.visibility != ShaderVisibility::Compute && sampler.visibility != ShaderVisibility::All) {
                return fail(ErrorCode::InvalidArgument, "Compute static sampler must have Compute or All visibility");
            }
        }
        std::vector<unsigned> computeMinimum(desc.bindingLayout.entries.size());
        auto code = vulkan::inspectSpirv(desc.compute, ShaderStage::Compute, 0, {}, nullptr, desc.bindingLayout, &computeMinimum);
        if (!code) return std::unexpected(code.error());

        auto p = std::make_shared<NativePipeline>();
        p->native = owner->native;
        p->isCompute = true;
        p->bindings = desc.bindingLayout;
        p->minimumBytes = std::move(computeMinimum);
        std::array<unsigned, 5> totals{};
        for (const auto& e : desc.bindingLayout.entries) {
            p->descriptorSize += e.count;
            const unsigned idx = isSampledTextureBinding(e.type) ? 0 :
                                 e.type == BindingType::ConstantBuffer ? 1 :
                                 e.type == BindingType::StorageBuffer ? 2 : 3;
            totals[idx] += e.count;
        }
        totals[4] = static_cast<unsigned>(desc.bindingLayout.samplers.size());
        const auto& limits = owner->limits;
        if (totals[0] > limits.maxPerStageDescriptorSampledImages ||
            totals[1] > limits.maxPerStageDescriptorUniformBuffers ||
            totals[2] > limits.maxPerStageDescriptorStorageBuffers ||
            totals[3] > limits.maxPerStageDescriptorStorageImages ||
            totals[4] > limits.maxPerStageDescriptorSamplers ||
            totals[0] + totals[1] + totals[2] + totals[3] > limits.maxPerStageResources) {
            return fail(ErrorCode::Unsupported, "Compute descriptor layout exceeds adapter limits");
        }
        p->samplers.reserve(desc.bindingLayout.samplers.size());
        for (const auto& s : desc.bindingLayout.samplers) {
            VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            info.magFilter = info.minFilter = s.filter == SamplerFilter::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            info.addressModeU = info.addressModeV = info.addressModeW = s.address == SamplerAddress::Clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
            info.maxLod = VK_LOD_CLAMP_NONE;
            VkSampler sampler{};
            if (auto ok = owner->accept(vulkan::acquireOutput(sampler, owner->native->CreateSampler, owner->native->handle, &info, nullptr), "vkCreateSampler"); !ok) return std::unexpected(ok.error());
            p->samplers.push_back(sampler);
            p->linear |= s.filter == SamplerFilter::Linear;
        }
        std::vector<VkDescriptorSetLayoutBinding> resources;
        for (unsigned i = 0; i < desc.bindingLayout.entries.size(); ++i) {
            const auto& e = desc.bindingLayout.entries[i];
            const VkDescriptorType dtype =
                isSampledTextureBinding(e.type) ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE :
                e.type == BindingType::ConstantBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER :
                e.type == BindingType::StorageBuffer ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER :
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            resources.push_back({i, dtype, e.count, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
        }
        for (unsigned i = 0; i < p->samplers.size(); ++i) {
            resources.push_back({32 + i, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &p->samplers[i]});
        }
        if (!resources.empty()) {
            VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            info.bindingCount = static_cast<unsigned>(resources.size());
            info.pBindings = resources.data();
            if (auto ok = owner->accept(vulkan::acquireOutput(p->setLayout, owner->native->CreateDescriptorSetLayout, owner->native->handle, &info, nullptr), "vkCreateDescriptorSetLayout"); !ok) return std::unexpected(ok.error());
        }
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        if (p->setLayout) {
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &p->setLayout;
        }
        if (auto ok = owner->accept(vulkan::acquireOutput(p->layout, owner->native->CreatePipelineLayout, owner->native->handle, &layoutInfo, nullptr), "vkCreatePipelineLayout"); !ok) return std::unexpected(ok.error());

        struct ModuleGuard {
            std::shared_ptr<NativeDevice> native;
            VkShaderModule module{};
            ~ModuleGuard() { if (module) native->DestroyShaderModule(native->handle, module, nullptr); }
        } guard{owner->native};
        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = code->size() * 4;
        moduleInfo.pCode = code->data();
        if (auto ok = owner->accept(vulkan::acquireOutput(guard.module, owner->native->CreateShaderModule, owner->native->handle, &moduleInfo, nullptr), "vkCreateShaderModule"); !ok) return std::unexpected(ok.error());

        VkComputePipelineCreateInfo pipeInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipeInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeInfo.stage.module = guard.module;
        pipeInfo.stage.pName = "main";
        pipeInfo.layout = p->layout;
        if (auto ok = owner->accept(owner->native->CreateComputePipelines(owner->native->handle, {}, 1, &pipeInfo, nullptr, &p->pipeline), "vkCreateComputePipelines"); !ok) return std::unexpected(ok.error());

        auto value = std::make_shared<VulkanPipeline>();
        value->owner = owner;
        value->native = p;
        value->token = std::make_shared<int>();
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        std::erase_if(owner->pipelines, [](auto& w) { return w.expired(); });
        owner->pipelines.push_back(p);
        owner->objects.push_back(value->token);
        return PipelinePtr(value);
    }
    Result<PipelinePtr> createMeshPipeline(const MeshPipelineDesc& desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        const auto targets = desc.renderTargets.value_or(RenderTargetLayout{{desc.colorFormat}, {}});
        if (auto ok = validateRenderTargets(targets, desc.depthState); !ok) return std::unexpected(ok.error());
        if (auto ok = validateBindingLayout(desc.bindingLayout); !ok) return std::unexpected(ok.error());
        if (desc.mesh.stage != ShaderStage::Mesh || desc.mesh.bytes.empty()) {
            return fail(ErrorCode::InvalidArgument, "MeshPipelineDesc requires a non-empty Mesh shader");
        }
        if (desc.task && (desc.task->stage != ShaderStage::Task || desc.task->bytes.empty())) {
            return fail(ErrorCode::InvalidArgument, "MeshPipelineDesc task shader must have Task stage and non-empty bytes");
        }
        if (!desc.fragment.bytes.empty() && desc.fragment.stage != ShaderStage::Fragment) {
            return fail(ErrorCode::InvalidArgument, "MeshPipelineDesc fragment shader must have Fragment stage");
        }
        if (!capabilities.meshShaders) {
            return fail(ErrorCode::Unsupported, "Vulkan adapter does not support VK_EXT_mesh_shader");
        }
        return fail(ErrorCode::Unsupported, "Mesh/Task shader pipeline execution is not yet implemented in this SDK snapshot");
    }
    Result<std::unique_ptr<CommandList>> createCommandList() override {
        return createCommandList(QueueType::Graphics);
    }
    Result<std::unique_ptr<CommandList>> createCommandList(QueueType type) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (!validQueueType(type)) return fail(ErrorCode::InvalidArgument, "Unknown queue type");
        auto value = std::make_unique<VulkanCommands>();
        value->owner = owner;
        value->qType = type;
        value->token = std::make_shared<int>();
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->objects.push_back(value->token);
        return std::unique_ptr<CommandList>(std::move(value));
    }
    Result<std::unique_ptr<CommandPool>> createCommandPool() override {
        return createCommandPool(QueueType::Graphics);
    }
    Result<std::unique_ptr<CommandPool>> createCommandPool(QueueType type) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (!validQueueType(type)) return fail(ErrorCode::InvalidArgument, "Unknown queue type");
        auto poolState = std::make_shared<NativeCommandPool>();
        poolState->native = owner->native;
        poolState->qType = type;
        poolState->recordingThread.store(owner->thread, std::memory_order_release);
        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        info.queueFamilyIndex = owner->queues[queueIndex(type)].family;
        if (auto ok = owner->accept(vulkan::acquireOutput(poolState->handle, owner->native->CreateCommandPool, owner->native->handle, &info, nullptr), "vkCreateCommandPool"); !ok) {
            return std::unexpected(ok.error());
        }
        auto value = std::make_unique<VulkanCommandPool>();
        value->owner = owner;
        value->pool = poolState;
        value->token = std::make_shared<int>();
        std::erase_if(owner->commandPools, [](auto& w) { return w.expired(); });
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->commandPools.push_back(poolState);
        owner->objects.push_back(value->token);
        return std::unique_ptr<CommandPool>(std::move(value));
    }
    Result<TimelineFencePtr> createTimelineFence(std::uint64_t initialValue) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (!owner->native->timelineSemaphores) {
            return fail(ErrorCode::Unsupported, "Adapter does not support VK_KHR_timeline_semaphore");
        }
        if (initialValue >= UINT64_MAX - 1) {
            return fail(ErrorCode::InvalidArgument, "Invalid initial timeline fence value");
        }
        auto sem = std::make_shared<NativeTimelineSemaphore>();
        sem->native = owner->native;
        sem->lastSignaled = initialValue;
        VkSemaphoreTypeCreateInfoKHR typeInfo{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR};
        typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
        typeInfo.initialValue = initialValue;
        VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        info.pNext = &typeInfo;
        if (auto ok = owner->accept(vulkan::acquireOutput(sem->handle, owner->native->CreateSemaphore, owner->native->handle, &info, nullptr), "vkCreateSemaphore timeline"); !ok) {
            return std::unexpected(ok.error());
        }
        auto value = std::make_shared<VulkanTimelineFence>();
        value->owner = owner;
        value->sem = sem;
        value->token = std::make_shared<int>();
        std::erase_if(owner->timelineSemaphores, [](auto& w) { return w.expired(); });
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->timelineSemaphores.push_back(sem);
        owner->objects.push_back(value->token);
        return TimelineFencePtr(std::move(value));
    }
    Result<void> checkSurfaceSupport(Surface surface) override {
        if (auto ok = owner->check(); !ok) return ok;
        auto checked = makeSurface(owner, surface);
        if (!checked) return std::unexpected(checked.error());
        return {};
    }
    Result<SurfacePresentCapabilities> querySurfacePresentCapabilities(Surface surface) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return queryVulkanSurfacePresentCapabilities(owner, surface);
    }
    Result<SurfaceColorCapabilities> querySurfaceColorCapabilities(Surface surface) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return queryVulkanSurfaceColorCapabilities(owner, surface);
    }
    Result<SurfaceMetrics> querySurfaceMetrics(Surface surface) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return queryVulkanSurfaceMetrics(owner, surface);
    }
    Result<std::unique_ptr<SwapChain>> createSwapChain(Surface surface, SwapChainDesc desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return makeSwapchain(owner, surface, desc);
    }
    CommandQueue& queue() override { return queues[0]; }
    Result<CommandQueue*> queue(QueueType type) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (!validQueueType(type)) return fail(ErrorCode::InvalidArgument, "Unknown queue type");
        return &queues[queueIndex(type)];
    }
    Result<std::uint64_t> validationErrorCount() override {
        if (std::this_thread::get_id() != owner->thread) return fail(ErrorCode::InvalidState, "Validation count requires creating thread");
        return owner->native->instance->validation.errors.load(std::memory_order_relaxed);
    }
};
} // namespace

Result<std::vector<AdapterInfo>> enumerateVulkanAdapters() {
    auto instance = createInstance(false); if (!instance) return std::unexpected(instance.error());
    auto found = candidates(*instance); if (!found) return std::unexpected(found.error());
    std::vector<AdapterInfo> result;
    for (auto& c : *found) result.push_back(std::move(c.info));
    return result;
}
Result<std::unique_ptr<Device>> createVulkanDevice(DeviceDesc desc) {
    if (desc.enableDeviceLossTesting) return fail(ErrorCode::Unsupported, "Vulkan loss injection unsupported");
    auto instance = createInstance(desc.validation); if (!instance) return std::unexpected(instance.error());
    auto found = candidates(*instance); if (!found) return std::unexpected(found.error());
    auto selected = std::find_if(found->begin(), found->end(), [&](auto& c) {
        return desc.adapter ? c.info.id == *desc.adapter : c.info.softwareAdapter == desc.softwareAdapter;
    });
    if (selected == found->end()) return fail(ErrorCode::Unsupported, "Requested Vulkan adapter unavailable");
    if (selected->info.softwareAdapter != desc.softwareAdapter) return fail(ErrorCode::InvalidArgument, "Vulkan adapter software-kind mismatch");
    auto native = std::make_shared<NativeDevice>(); native->instance = *instance;
    float priority = 1.f;
    std::vector<std::uint32_t> uniqueFamilies;
    for (std::uint32_t f : selected->families) {
        if (std::find(uniqueFamilies.begin(), uniqueFamilies.end(), f) == uniqueFamilies.end()) {
            uniqueFamilies.push_back(f);
        }
    }
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(uniqueFamilies.size());
    for (std::uint32_t f : uniqueFamilies) {
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = f;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        queueInfos.push_back(q);
    }
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
    info.pQueueCreateInfos = queueInfos.data();
    std::uint32_t extensionCount{};
    auto er = (*instance)->EnumerateDeviceExtensionProperties(selected->physical, nullptr, &extensionCount, nullptr);
    if (er != VK_SUCCESS) return nativeError(er, "Enumerate device extensions");
    std::vector<VkExtensionProperties> extensions(extensionCount);
    er = (*instance)->EnumerateDeviceExtensionProperties(selected->physical, nullptr, &extensionCount, extensions.data());
    if (er != VK_SUCCESS) return nativeError(er, "Enumerate device extensions");
    extensions.resize(extensionCount);
    const auto hasExtension = [&](const char* name) { return std::any_of(extensions.begin(), extensions.end(), [&](auto& p) { return std::strcmp(p.extensionName, name) == 0; }); };
    std::vector<const char*> enabledDeviceExtensions;
    void* featureChain = nullptr;
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT};
    if ((*instance)->presentation && hasExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME) && hasExtension(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) {
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; features.pNext = &maintenance;
        (*instance)->GetPhysicalDeviceFeatures2(selected->physical, &features);
        native->presentation = maintenance.swapchainMaintenance1 == VK_TRUE;
        if (native->presentation) {
            enabledDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
            enabledDeviceExtensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
            maintenance.pNext = featureChain;
            featureChain = &maintenance;
            if (hasExtension(VK_EXT_HDR_METADATA_EXTENSION_NAME)) {
                native->hdrMetadata = true;
                enabledDeviceExtensions.push_back(VK_EXT_HDR_METADATA_EXTENSION_NAME);
            }
            if (hasExtension(VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME)) {
                native->incrementalPresent = true;
                enabledDeviceExtensions.push_back(VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME);
            }
        }
    }
    VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timelineFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR};
    if (hasExtension(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)) {
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; features.pNext = &timelineFeatures;
        (*instance)->GetPhysicalDeviceFeatures2(selected->physical, &features);
        if (timelineFeatures.timelineSemaphore == VK_TRUE) {
            native->timelineSemaphores = true;
            enabledDeviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
            timelineFeatures.pNext = featureChain;
            featureChain = &timelineFeatures;
        }
    }
    bool descriptorIndexingSupported = false;
    std::uint32_t maxBindless = 0u;
    if (hasExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME)) {
        VkPhysicalDeviceDescriptorIndexingFeaturesEXT indexingFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &indexingFeatures;
        (*instance)->GetPhysicalDeviceFeatures2(selected->physical, &features);
        if (indexingFeatures.shaderSampledImageArrayNonUniformIndexing == VK_TRUE &&
            indexingFeatures.descriptorBindingPartiallyBound == VK_TRUE &&
            indexingFeatures.runtimeDescriptorArray == VK_TRUE) {
            descriptorIndexingSupported = true;
            VkPhysicalDeviceDescriptorIndexingPropertiesEXT indexingProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES_EXT};
            VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            props.pNext = &indexingProps;
            (*instance)->GetPhysicalDeviceProperties2(selected->physical, &props);
            maxBindless = std::min<std::uint32_t>(1000000u, indexingProps.maxPerStageDescriptorUpdateAfterBindSampledImages);
            if (!maxBindless) maxBindless = std::min<std::uint32_t>(1000000u, selected->limits.maxPerStageDescriptorSampledImages);
        }
    }
    const bool meshShaderSupported = hasExtension("VK_EXT_mesh_shader");
    info.enabledExtensionCount = static_cast<std::uint32_t>(enabledDeviceExtensions.size());
    info.ppEnabledExtensionNames = enabledDeviceExtensions.empty() ? nullptr : enabledDeviceExtensions.data();
    info.pNext = featureChain;
    VkPhysicalDeviceFeatures enabled{};
    enabled.fullDrawIndexUint32 = selected->features.fullDrawIndexUint32;
    enabled.multiDrawIndirect = selected->features.multiDrawIndirect;
    enabled.textureCompressionBC = selected->features.textureCompressionBC;
    enabled.textureCompressionETC2 = selected->features.textureCompressionETC2;
    enabled.textureCompressionASTC_LDR = selected->features.textureCompressionASTC_LDR;
    enabled.sparseBinding = selected->features.sparseBinding;
    enabled.sparseResidencyImage2D = selected->features.sparseBinding ? selected->features.sparseResidencyImage2D : VK_FALSE;
    info.pEnabledFeatures = &enabled;
    if (auto r = vulkan::acquireOutput(native->handle, (*instance)->CreateDevice, selected->physical, &info, nullptr); r != VK_SUCCESS) return nativeError(r, "vkCreateDevice");
#define LOAD(name) native->name = reinterpret_cast<PFN_vk##name>((*instance)->GetDeviceProcAddr(native->handle, "vk" #name)); \
    if (!native->name) return fail(ErrorCode::Unsupported, "Vulkan device entry point missing: vk" #name);
    DEVICE_FUNCTIONS(LOAD)
#undef LOAD
    if ((*instance)->debugUtils)
        native->CmdInsertDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>((*instance)->GetDeviceProcAddr(native->handle, "vkCmdInsertDebugUtilsLabelEXT"));
    if (native->presentation) {
#define LOAD_WSI(name) native->name = reinterpret_cast<PFN_vk##name>((*instance)->GetDeviceProcAddr(native->handle, "vk" #name)); if (!native->name) return fail(ErrorCode::Unsupported, "Vulkan swapchain entry point missing: vk" #name);
        LOAD_WSI(CreateSwapchainKHR) LOAD_WSI(DestroySwapchainKHR) LOAD_WSI(GetSwapchainImagesKHR) LOAD_WSI(AcquireNextImageKHR) LOAD_WSI(QueuePresentKHR)
#undef LOAD_WSI
        if (native->hdrMetadata) {
            native->SetHdrMetadataEXT = reinterpret_cast<PFN_vkSetHdrMetadataEXT>((*instance)->GetDeviceProcAddr(native->handle, "vkSetHdrMetadataEXT"));
            if (!native->SetHdrMetadataEXT) native->hdrMetadata = false;
        }
    }
    if (native->timelineSemaphores) {
        native->GetSemaphoreCounterValueKHR = reinterpret_cast<PFN_vkGetSemaphoreCounterValueKHR>((*instance)->GetDeviceProcAddr(native->handle, "vkGetSemaphoreCounterValueKHR"));
        native->WaitSemaphoresKHR = reinterpret_cast<PFN_vkWaitSemaphoresKHR>((*instance)->GetDeviceProcAddr(native->handle, "vkWaitSemaphoresKHR"));
        native->SignalSemaphoreKHR = reinterpret_cast<PFN_vkSignalSemaphoreKHR>((*instance)->GetDeviceProcAddr(native->handle, "vkSignalSemaphoreKHR"));
        if (!native->GetSemaphoreCounterValueKHR || !native->WaitSemaphoresKHR || !native->SignalSemaphoreKHR) {
            native->timelineSemaphores = false;
        }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        native->GetDeviceQueue(native->handle, selected->families[i], 0, &native->queues[i]);
    }
    native->queue = native->queues[0];
    auto owner = std::make_shared<State>(); owner->native = native; owner->physical = selected->physical;
    if (!owner->id) return fail(ErrorCode::InvalidState, "Device generation sequence exhausted");
    owner->memory = selected->memory;
    owner->family = selected->family;
    owner->uniqueFamilies = std::move(uniqueFamilies);
    owner->timestampBits = selected->timestampBits[0];
    for (std::size_t i = 0; i < 3; ++i) {
        owner->queues[i].family = selected->families[i];
        owner->queues[i].timestampBits = selected->timestampBits[i];
        owner->queues[i].id = (i == 0) ? owner->id : detail::allocateDeviceGeneration();
        if (!owner->queues[i].id) return fail(ErrorCode::InvalidState, "Queue generation sequence exhausted");
    }
    owner->maxColorAttachments = selected->maxColorAttachments;
    owner->maxFramebufferWidth = selected->maxFramebufferWidth; owner->maxFramebufferHeight = selected->maxFramebufferHeight;
    owner->limits = selected->limits;
    owner->fullDrawIndexUint32 = enabled.fullDrawIndexUint32 != 0;
    owner->multiDrawIndirect = enabled.multiDrawIndirect != 0;
    owner->maxDrawIndirectCount = owner->multiDrawIndirect
        ? std::min<std::uint32_t>(65536u, std::max<std::uint32_t>(1u, owner->limits.maxDrawIndirectCount))
        : 1u;
    owner->textureCompressionBC = enabled.textureCompressionBC != 0;
    owner->textureCompressionETC2 = enabled.textureCompressionETC2 != 0;
    owner->textureCompressionASTC_LDR = enabled.textureCompressionASTC_LDR != 0;
    auto device = std::make_unique<VulkanDevice>();
    device->attachOwner(owner);
    const std::array<QueueType, 3> queueTypes{QueueType::Graphics, QueueType::Compute, QueueType::Copy};
    for (std::size_t i = 0; i < 3; ++i) {
        device->queues[i].owner = owner;
        device->queues[i].qType = queueTypes[i];
    }
    device->description = desc; device->description.adapter = selected->info.id;
    device->capabilities.backend = Backend::Vulkan; device->capabilities.adapterName = selected->info.name;
    device->capabilities.softwareAdapter = selected->info.softwareAdapter; device->capabilities.adapterId = selected->info.id;
    device->capabilities.deviceRecovery = true;
    device->capabilities.multipleRenderTargets = selected->maxColorAttachments >= 2;
    device->capabilities.vertexInput = device->capabilities.indexedDrawing = true;
    device->capabilities.timestampQueries = vulkan::timestampSupported(owner->timestampBits, owner->limits.timestampPeriod);
#if defined(_WIN32) || defined(GFX_HAS_VULKAN_XCB)
    device->capabilities.swapChain = native->presentation;
#endif
    device->capabilities.resourceBinding = device->capabilities.bindingLayouts = true;
    device->capabilities.compute = true;
    device->capabilities.dedicatedComputeQueue = selected->dedicatedComputeQueue;
    device->capabilities.dedicatedCopyQueue = selected->dedicatedCopyQueue;
    device->capabilities.timelineFences = native->timelineSemaphores;
    device->capabilities.resourceAliasing = true;
    device->capabilities.residencyControl = false;
    device->capabilities.sparseBinding = enabled.sparseBinding != 0;
    device->capabilities.sparseResidencyTexture2D = enabled.sparseResidencyImage2D != 0;
    device->capabilities.indirectDraw = true;
    device->capabilities.multiDrawIndirect = owner->multiDrawIndirect;
    device->capabilities.descriptorIndexing = descriptorIndexingSupported;
    device->capabilities.meshShaders = meshShaderSupported;
    device->capabilities.maxDrawIndirectCount = owner->maxDrawIndirectCount;
    device->capabilities.maxBindlessDescriptors = maxBindless;
    VkImageFormatProperties depthProperties{};
    device->capabilities.depthAttachment = owner->native->instance->GetPhysicalDeviceImageFormatProperties(owner->physical, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0, &depthProperties) == VK_SUCCESS && (depthProperties.sampleCounts & VK_SAMPLE_COUNT_1_BIT);
    device->capabilities.maxTextureDimension2D = std::min<std::uint32_t>(16384u, owner->limits.maxImageDimension2D);
    device->capabilities.maxColorAttachments = std::min<std::uint32_t>(8u, owner->maxColorAttachments);
    device->capabilities.constantBufferAlignment = static_cast<std::uint32_t>(
        std::max<std::uint64_t>(256u, owner->limits.minUniformBufferOffsetAlignment));
    device->capabilities.textureCopyRowAlignment = 256u;
    device->capabilities.textureCopyOffsetAlignment = 512u;
    device->capabilities.maxVertexStreams = std::min<std::uint32_t>(32u, owner->limits.maxVertexInputBindings);
    device->capabilities.maxConstantBufferSize = std::min<std::uint32_t>(65536u, owner->limits.maxUniformBufferRange);
    return std::unique_ptr<Device>(std::move(device));
}
} // namespace gfx
