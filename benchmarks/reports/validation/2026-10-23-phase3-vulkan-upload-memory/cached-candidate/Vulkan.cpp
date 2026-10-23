#include <graphics/Graphics.hpp>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include "VulkanValidation.hpp"
#include "Spirv.hpp"
#include "Timestamp.hpp"
#include "MemoryType.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <list>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#include <vulkan/vulkan_win32.h>
#else
#include <dlfcn.h>
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
    PFN_vkDestroySurfaceKHR DestroySurfaceKHR{};
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR{};
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR{};
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR{};
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR{};
#ifdef _WIN32
    PFN_vkCreateWin32SurfaceKHR CreateWin32SurfaceKHR{};
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
#ifdef _WIN32
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
        instance->presentation = has(VK_KHR_SURFACE_EXTENSION_NAME) && has(VK_KHR_WIN32_SURFACE_EXTENSION_NAME) &&
            has(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) && has(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        if (instance->presentation) enabledExtensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
            VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME};
    }
#endif
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
        enabledExtensions.insert(enabledExtensions.end(), std::begin(vulkan::ValidationConfiguration::extensions), std::end(vulkan::ValidationConfiguration::extensions));
        info.pNext = &validationConfig.layerSettings;
    }
    info.enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size()); info.ppEnabledExtensionNames = enabledExtensions.data();
    if (auto r = create(&info, nullptr, &instance->handle); r != VK_SUCCESS) return nativeError(r, "vkCreateInstance");
#define LOAD(name) instance->name = reinterpret_cast<PFN_vk##name>(get(instance->handle, "vk" #name)); \
    if (!instance->name) return fail(ErrorCode::Unsupported, "Vulkan instance entry point missing: vk" #name);
    INSTANCE_FUNCTIONS(LOAD)
#undef LOAD
    if (instance->presentation) {
#define LOAD_WSI(name) instance->name = reinterpret_cast<PFN_vk##name>(get(instance->handle, "vk" #name)); if (!instance->name) return fail(ErrorCode::Unsupported, "Vulkan surface entry point missing: vk" #name);
        LOAD_WSI(DestroySurfaceKHR) LOAD_WSI(GetPhysicalDeviceSurfaceSupportKHR) LOAD_WSI(GetPhysicalDeviceSurfaceCapabilitiesKHR)
        LOAD_WSI(GetPhysicalDeviceSurfaceFormatsKHR) LOAD_WSI(GetPhysicalDeviceSurfacePresentModesKHR)
#ifdef _WIN32
        LOAD_WSI(CreateWin32SurfaceKHR)
#endif
#undef LOAD_WSI
    }
    if (validation) {
        const auto make = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(get(instance->handle, "vkCreateDebugUtilsMessengerEXT"));
        instance->destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(get(instance->handle, "vkDestroyDebugUtilsMessengerEXT"));
        if (!make || !instance->destroyMessenger) return fail(ErrorCode::Unsupported, "Vulkan debug messenger unavailable");
        if (auto r = make(instance->handle, &validationConfig.messenger, nullptr, &instance->messenger); r != VK_SUCCESS) return nativeError(r, "Create validation messenger");
    }
    return instance;
}
struct Candidate {
    AdapterInfo info;
    VkPhysicalDevice physical{};
    VkPhysicalDeviceMemoryProperties memory{};
    std::uint32_t family{};
    std::uint32_t timestampBits{};
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
        auto queue = std::find_if(queues.begin(), queues.end(), [](auto& q) { return q.queueCount && (q.queueFlags & VK_QUEUE_GRAPHICS_BIT); });
        if (queue == queues.end()) continue;
        Candidate c; c.physical = device; c.family = static_cast<std::uint32_t>(queue - queues.begin());
        c.timestampBits = queue->timestampValidBits;
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
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) X(CmdCopyBufferToImage) X(CmdCopyImageToBuffer) \
    X(CreateImageView) X(DestroyImageView) X(CreateRenderPass) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) X(CmdBeginRenderPass) X(CmdEndRenderPass) \
    X(CreateShaderModule) X(DestroyShaderModule) X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateGraphicsPipelines) X(DestroyPipeline) \
    X(CreateSampler) X(DestroySampler) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CmdBindDescriptorSets) \
    X(CreateQueryPool) X(DestroyQueryPool) X(CmdResetQueryPool) X(CmdWriteTimestamp) X(GetQueryPoolResults) \
    X(CmdBindPipeline) X(CmdSetViewport) X(CmdSetScissor) X(CmdDraw) X(CmdBindVertexBuffers) X(CmdBindIndexBuffer) X(CmdDrawIndexed) \
    X(MapMemory) X(UnmapMemory) X(FlushMappedMemoryRanges) X(InvalidateMappedMemoryRanges) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(CmdCopyBuffer) X(CmdPipelineBarrier) X(CreateFence) X(DestroyFence) X(GetFenceStatus) X(WaitForFences) X(CreateSemaphore) X(DestroySemaphore)
struct NativeDevice {
    std::shared_ptr<Instance> instance;
    VkDevice handle{};
    VkQueue queue{};
    bool presentation{};
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR{};
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR{};
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR{};
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR{};
    PFN_vkQueuePresentKHR QueuePresentKHR{};
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
    ~NativeSurface() { if (handle) native->instance->DestroySurfaceKHR(native->instance->handle, handle, nullptr); }
};
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
struct Allocation {
    std::shared_ptr<NativeDevice> native;
    VkBuffer buffer{};
    VkImage image{};
    VkImageView view{};
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
    std::uint64_t version{}, lastUse{};
    void release() {
        if (buffer) native->DestroyBuffer(native->handle, buffer, nullptr);
        if (view) native->DestroyImageView(native->handle, view, nullptr);
        if (image && !swapchain) native->DestroyImage(native->handle, image, nullptr);
        if (memory) native->FreeMemory(native->handle, memory, nullptr);
        buffer = {}; view = {}; image = {}; memory = {};
    }
    ~Allocation() { release(); }
};
bool currentBackbuffer(const Allocation& a) {
    if (!a.swapchain) return true;
    const auto& s = *a.swapchain;
    bool liveWindow = true;
#ifdef _WIN32
    liveWindow = IsWindow(static_cast<HWND>(s.surface->window)) != FALSE;
#endif
    return liveWindow && s.alive && !s.invalid && s.acquired && s.current == a.imageIndex;
}
struct Use { std::shared_ptr<Allocation> allocation; std::uint64_t version; ResourceState shadow; bool imageInitialized, contentsInitialized; };
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
    DepthState depthState;
    struct Input { std::uint32_t slot{}, stride{}, extent{}, alignment{1}; VertexInputRate rate{}; };
    std::vector<Input> inputs;
    BindingLayout bindings;
    std::vector<unsigned> minimumBytes;
    std::vector<VkSampler> samplers;
    VkDescriptorSetLayout setLayout{};
    unsigned descriptorSize{};
    bool sampled{}, linear{};
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
struct NativeBindings {
    std::shared_ptr<NativeDevice> native;
    std::shared_ptr<NativePipeline> pipeline;
    std::vector<std::shared_ptr<Allocation>> resources;
    VkDescriptorPool pool{};
    VkDescriptorSet set{};
    ~NativeBindings() { if (pool) native->DestroyDescriptorPool(native->handle, pool, nullptr); }
};
struct Recording {
    std::shared_ptr<NativeDevice> native;
    VkCommandPool pool{};
    VkCommandBuffer command{};
    bool open{}, ended{}, inPass{};
    std::uint64_t submitted{};
    std::vector<Use> uses;
    std::vector<std::unique_ptr<NativePass>> passes;
    std::vector<std::shared_ptr<NativePipeline>> pipelines;
    std::shared_ptr<NativePipeline> selectedPipeline;
    std::vector<Format> passColors;
    bool passDepth{}, depthReadOnly{};
    std::uint32_t width{}, height{};
    std::array<VertexView, 32> vertices;
    IndexView indices;
    std::vector<std::unique_ptr<NativeBindings>> snapshots;
    NativeBindings* selectedBindings{};
    unsigned descriptors{};
    VkQueryPool queries{};
    std::array<bool, 2> timestampWritten{};
    bool timestampsResolved{}, timestampStartFirst{};
    std::chrono::steady_clock::time_point submittedAt;
    std::optional<double> timestampResult;
    void release() {
        if (pool) native->DestroyCommandPool(native->handle, pool, nullptr);
        if (queries) native->DestroyQueryPool(native->handle, queries, nullptr);
        queries = {}; timestampWritten = {}; timestampsResolved = timestampStartFirst = false; timestampResult.reset();
        pool = {}; command = {}; selectedBindings = {}; snapshots.clear(); descriptors = 0; selectedPipeline.reset(); pipelines.clear(); vertices = {}; indices = {}; passColors.clear(); passes.clear(); uses.clear(); open = ended = inPass = false;
    }
    ~Recording() { release(); }
};
struct Pending { VkFence fence{}; std::uint64_t serial{}; std::shared_ptr<Recording> recording; };
std::atomic<std::uint64_t> generations{1};
struct State {
    std::shared_ptr<NativeDevice> native;
    VkPhysicalDevice physical{};
    VkPhysicalDeviceMemoryProperties memory{};
    std::uint32_t family{};
    std::uint32_t timestampBits{};
    std::uint32_t maxColorAttachments{};
    std::uint32_t maxFramebufferWidth{}, maxFramebufferHeight{};
    VkPhysicalDeviceLimits limits{};
    bool fullDrawIndexUint32{};
    std::thread::id thread{std::this_thread::get_id()};
    std::uint64_t id{generations.fetch_add(1)}, last{}, completed{};
    bool retired{}, lost{};
    std::list<Pending> pending;
    std::vector<std::weak_ptr<Allocation>> allocations;
    std::vector<std::weak_ptr<Recording>> recordings;
    std::vector<std::weak_ptr<NativePipeline>> pipelines;
    std::vector<std::weak_ptr<NativeSwapchain>> swapchains;
    std::vector<std::weak_ptr<int>> objects;
    Result<void> check() const {
        if (std::this_thread::get_id() != thread) return fail(ErrorCode::InvalidState, "Vulkan operations require the creating thread");
        if (retired || lost) return fail(ErrorCode::DeviceLost, "Vulkan device generation retired or lost", VK_ERROR_DEVICE_LOST);
        return {};
    }
    Result<void> accept(VkResult r, const char* operation) {
        if (r == VK_ERROR_DEVICE_LOST) lost = true;
        if (r != VK_SUCCESS) return nativeError(r, operation);
        return {};
    }
    Result<void> poll() {
        if (auto ok = check(); !ok) return ok;
        while (!pending.empty()) {
            auto& first = pending.front();
            const auto r = native->GetFenceStatus(native->handle, first.fence);
            if (r == VK_NOT_READY) break;
            if (auto ok = accept(r, "vkGetFenceStatus"); !ok) return ok;
            completed = first.serial;
            native->DestroyFence(native->handle, first.fence, nullptr); pending.pop_front();
        }
        return {};
    }
    void retire() {
        if (retired || !native) return;
        // The device is the lifetime boundary; public children remain inert descriptions.
        native->DeviceWaitIdle(native->handle);
        for (auto& p : pending) native->DestroyFence(native->handle, p.fence, nullptr);
        pending.clear();
        for (auto& weak : recordings) if (auto value = weak.lock()) value->release();
        for (auto& weak : pipelines) if (auto value = weak.lock()) value->release();
        for (auto& weak : allocations) if (auto value = weak.lock()) value->release();
        for (auto& weak : swapchains) if (auto value = weak.lock()) value->release();
        swapchains.clear();
        recordings.clear(); pipelines.clear(); allocations.clear(); objects.clear();
        native->release(); retired = true;
    }
    ~State() { retire(); }
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
        if (a.lastUse > owner->completed || a.version == UINT64_MAX) return fail(ErrorCode::InvalidState, "Upload buffer busy or version exhausted");
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
        if (a.lastUse > owner->completed) return fail(ErrorCode::InvalidState, "Readback buffer busy");
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
struct VulkanPipeline final : Pipeline {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativePipeline> native;
    std::shared_ptr<int> token;
};
VkImageLayout imageLayout(ResourceState state) {
    if (state == ResourceState::Present) return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    if (state == ResourceState::CopySource) return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    if (state == ResourceState::CopyDestination) return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if (state == ResourceState::RenderTarget) return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if (state == ResourceState::ShaderRead) return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (state == ResourceState::DepthWrite) return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if (state == ResourceState::DepthRead) return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    return VK_IMAGE_LAYOUT_GENERAL;
}
VkAccessFlags access(ResourceState state) {
    if (state == ResourceState::Present) return 0;
    if (state == ResourceState::CopySource) return VK_ACCESS_TRANSFER_READ_BIT;
    if (state == ResourceState::CopyDestination) return VK_ACCESS_TRANSFER_WRITE_BIT;
    if (state == ResourceState::RenderTarget) return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (state == ResourceState::ShaderRead) return VK_ACCESS_SHADER_READ_BIT;
    if (state == ResourceState::ConstantBuffer) return VK_ACCESS_UNIFORM_READ_BIT;
    if (state == ResourceState::DepthWrite) return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (state == ResourceState::DepthRead) return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    if (state == ResourceState::VertexBuffer) return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    if (state == ResourceState::IndexBuffer) return VK_ACCESS_INDEX_READ_BIT;
    if (state == ResourceState::VertexAndIndexBuffer) return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
    return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
}
VkFormat textureFormat(Format f) { return f == Format::D32Float ? VK_FORMAT_D32_SFLOAT : f == Format::RGBA8UNorm ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT; }
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
    std::shared_ptr<Recording> record;
    std::shared_ptr<int> token;
    Result<void> ready() const {
        if (auto ok = owner->check(); !ok) return ok;
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
    Use* find(const std::shared_ptr<Allocation>& a) {
        const auto it = std::find_if(record->uses.begin(), record->uses.end(), [&](auto& use) { return use.allocation == a; });
        return it == record->uses.end() ? nullptr : &*it;
    }
    ResourceState shadow(const std::shared_ptr<Allocation>& a) { auto use = find(a); return use ? use->shadow : a->state; }
    Use& retain(const std::shared_ptr<Allocation>& a) {
        if (auto use = find(a)) return *use;
        return record->uses.emplace_back(Use{a, a->version, a->state, a->imageInitialized, a->contentsInitialized});
    }
    Result<void> begin() override {
        if (auto ok = owner->poll(); !ok) return ok;
        if (record && (record->open || record->submitted > owner->completed)) return fail(ErrorCode::InvalidState, "Command list open or in flight");
        auto next = std::make_shared<Recording>(); next->native = owner->native;
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex = owner->family;
        if (auto ok = owner->accept(owner->native->CreateCommandPool(owner->native->handle, &pool, nullptr, &next->pool), "vkCreateCommandPool"); !ok) return ok;
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = next->pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
        if (auto ok = owner->accept(owner->native->AllocateCommandBuffers(owner->native->handle, &allocate, &next->command), "vkAllocateCommandBuffers"); !ok) return ok;
        VkCommandBufferBeginInfo start{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; start.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (auto ok = owner->accept(owner->native->BeginCommandBuffer(next->command, &start), "vkBeginCommandBuffer"); !ok) return ok;
        if (vulkan::timestampSupported(owner->timestampBits, owner->limits.timestampPeriod)) {
            VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; queries.queryType = VK_QUERY_TYPE_TIMESTAMP; queries.queryCount = 2;
            if (auto ok = owner->accept(owner->native->CreateQueryPool(owner->native->handle, &queries, nullptr, &next->queries), "vkCreateQueryPool"); !ok) return ok;
            owner->native->CmdResetQueryPool(next->command, next->queries, 0, 2);
        }
        next->open = true;
        std::erase_if(owner->recordings, [](auto& w) { return w.expired(); });
        owner->recordings.push_back(next); record = std::move(next); return {};
    }
    Result<void> end() override {
        if (auto ok = outsidePass(); !ok) return ok;
        const auto r = owner->native->EndCommandBuffer(record->command);
        record->open = false; record->ended = r == VK_SUCCESS;
        return owner->accept(r, "vkEndCommandBuffer");
    }
    Result<void> transition(const BufferPtr& value, ResourceState target) override {
        if (auto ok = outsidePass(); !ok) return ok;
        const auto a = buffer(value); if (!a) return std::unexpected(a.error());
        if (target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination && !vertexState(target) && !indexState(target) && target != ResourceState::ConstantBuffer)
            return fail(ErrorCode::Unsupported, "Vulkan transfer state unsupported");
        if (((*a)->desc.memory == Memory::Upload && target != ResourceState::CopySource && !vertexState(target) && !indexState(target) && target != ResourceState::ConstantBuffer) ||
            ((*a)->desc.memory == Memory::Readback && target != ResourceState::CopyDestination))
            return fail(ErrorCode::InvalidArgument, "Host buffer state is fixed");
        record->uses.reserve(record->uses.size() + 1);
        auto& use = retain(*a);
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask = access(use.shadow) | ((*a)->desc.memory == Memory::Upload ? VK_ACCESS_HOST_WRITE_BIT : 0);
        barrier.dstAccessMask = access(target) | ((*a)->desc.memory == Memory::Readback ? VK_ACCESS_HOST_READ_BIT : 0);
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = (*a)->buffer; barrier.size = VK_WHOLE_SIZE;
        owner->native->CmdPipelineBarrier(record->command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
        use.shadow = target; return {};
    }
    Result<void> copyBuffer(const BufferPtr& source, const BufferPtr& destination, std::uint64_t bytes) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = buffer(source); if (!src) return std::unexpected(src.error());
        auto dst = buffer(destination); if (!dst) return std::unexpected(dst.error());
        if (!bytes || bytes > (*src)->desc.size || bytes > (*dst)->desc.size || *src == *dst)
            return fail(ErrorCode::InvalidArgument, "Buffer copy range or alias invalid");
        if (shadow(*src) != ResourceState::CopySource || shadow(*dst) != ResourceState::CopyDestination)
            return fail(ErrorCode::InvalidState, "Buffer copy requires explicit source/destination states");
        // Finish all validation and allocations before retaining either argument.
        record->uses.reserve(record->uses.size() + 2); retain(*src); retain(*dst);
        VkBufferMemoryBarrier before[2]{};
        for (auto& b : before) { b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.size = VK_WHOLE_SIZE; }
        before[0].buffer = (*src)->buffer; before[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT; before[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        before[1].buffer = (*dst)->buffer; before[1].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_READ_BIT; before[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        owner->native->CmdPipelineBarrier(record->command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 2, before, 0, nullptr);
        VkBufferCopy region{0, 0, bytes};
        owner->native->CmdCopyBuffer(record->command, (*src)->buffer, (*dst)->buffer, 1, &region);
        if ((*dst)->desc.memory == Memory::Readback) {
            auto after = before[1]; after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; after.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            owner->native->CmdPipelineBarrier(record->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &after, 0, nullptr);
        }
        return {};
    }
    Result<void> transition(const TexturePtr& value, ResourceState target) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto a = texture(value); if (!a) return std::unexpected(a.error());
        if (target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination && target != ResourceState::RenderTarget && target != ResourceState::ShaderRead && target != ResourceState::DepthWrite && target != ResourceState::DepthRead && target != ResourceState::Present)
            return fail(ErrorCode::Unsupported, "Vulkan texture transfer state unsupported");
        if (target == ResourceState::Present && !(*a)->swapchain) return fail(ErrorCode::InvalidArgument, "Present requires a swapchain backbuffer");
        if (target == ResourceState::ShaderRead && (*a)->swapchain) return fail(ErrorCode::Unsupported, "Backbuffer sampling unsupported");
        if ((target == ResourceState::DepthWrite || target == ResourceState::DepthRead) && !(*a)->textureDesc.depthStencil)
            return fail(ErrorCode::InvalidArgument, "Depth state requires depth texture");
        if (target == ResourceState::ShaderRead && (*a)->textureDesc.depthStencil) return fail(ErrorCode::Unsupported, "Depth sampling unsupported");
        if (target == ResourceState::RenderTarget && !(*a)->textureDesc.renderTarget)
            return fail(ErrorCode::InvalidArgument, "Texture was not created as a render target");
        record->uses.reserve(record->uses.size() + 1);
        auto& use = retain(*a);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = use.imageInitialized ? imageLayout(use.shadow) : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = imageLayout(target);
        barrier.srcAccessMask = use.imageInitialized ? access(use.shadow) : 0;
        barrier.dstAccessMask = access(target);
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = (*a)->image; barrier.subresourceRange = {imageAspect(**a), 0, 1, 0, 1};
        owner->native->CmdPipelineBarrier(record->command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        use.shadow = target; use.imageInitialized = true; return {};
    }
    Result<void> copyTexture(const std::shared_ptr<Allocation>& image, const std::shared_ptr<Allocation>& buffer, bool upload) {
        if (buffer->desc.size < image->footprint.size ||
            buffer->desc.memory == (upload ? Memory::Readback : Memory::Upload))
            return fail(ErrorCode::InvalidArgument, "Texture copy buffer size or memory kind invalid");
        if (shadow(image) != (upload ? ResourceState::CopyDestination : ResourceState::CopySource) ||
            shadow(buffer) != (upload ? ResourceState::CopySource : ResourceState::CopyDestination))
            return fail(ErrorCode::InvalidState, "Texture copy requires explicit source/destination states");
        // Validate both arguments before taking references or version snapshots.
        record->uses.reserve(record->uses.size() + 2); retain(image); retain(buffer);
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
        i.image = image->image; i.subresourceRange = {imageAspect(*image), 0, 1, 0, 1};
        owner->native->CmdPipelineBarrier(record->command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &b, 1, &i);
        VkBufferImageCopy region{};
        region.bufferRowLength = image->footprint.rowPitch / texelBytes(image->textureDesc.format);
        region.imageSubresource = {imageAspect(*image), 0, 0, 1};
        region.imageExtent = {image->textureDesc.width, image->textureDesc.height, 1};
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
    Result<void> beginRenderPass(const TexturePtr& target, Color clear) override {
        return beginRenderPass(RenderPassDesc{{{target, LoadOp::Clear, clear}}, {}});
    }
    Result<void> beginRenderPass(const RenderPassDesc& description) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (auto ok = validateRenderPass(description); !ok) return ok;
        if (description.colors.size() > owner->maxColorAttachments)
            return fail(ErrorCode::Unsupported, "Color attachment count exceeds adapter limit");
        std::vector<std::shared_ptr<Allocation>> colors;
        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> references;
        std::vector<VkImageView> views;
        std::vector<VkClearValue> clears;
        std::vector<Format> formats;
        for (const auto& color : description.colors) {
            auto a = texture(color.texture); if (!a) return std::unexpected(a.error());
            if (shadow(*a) != ResourceState::RenderTarget) return fail(ErrorCode::InvalidState, "Attachment requires RenderTarget state");
            const auto use = find(*a);
            if (color.load == LoadOp::Load && !(use ? use->contentsInitialized : (*a)->contentsInitialized))
                return fail(ErrorCode::InvalidState, "Load requires initialized attachment contents");
            colors.push_back(*a); views.push_back((*a)->view);
            formats.push_back((*a)->textureDesc.format);
            VkAttachmentDescription attachment{};
            attachment.format = (*a)->textureDesc.format == Format::RGBA8UNorm ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT;
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = color.load == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            references.push_back({static_cast<std::uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
            attachments.push_back(attachment);
            VkClearValue clear{}; clear.color = {{color.clear.r, color.clear.g, color.clear.b, color.clear.a}}; clears.push_back(clear);
        }
        VkAttachmentReference depthReference{};
        if (description.depth.texture) {
            const auto& depth = description.depth;
            auto a = texture(depth.texture); if (!a) return std::unexpected(a.error());
            if (shadow(*a) != (depth.readOnly ? ResourceState::DepthRead : ResourceState::DepthWrite)) return fail(ErrorCode::InvalidState, "Depth attachment read/write state mismatch");
            const auto use = find(*a);
            if (depth.load == LoadOp::Load && !(use ? use->contentsInitialized : (*a)->contentsInitialized)) return fail(ErrorCode::InvalidState, "Depth Load requires initialized contents");
            colors.push_back(*a); views.push_back((*a)->view);
            VkAttachmentDescription attachment{}; attachment.format = VK_FORMAT_D32_SFLOAT; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = depth.load == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = attachment.finalLayout = imageLayout(shadow(*a));
            depthReference = {static_cast<unsigned>(attachments.size()), attachment.initialLayout}; attachments.push_back(attachment);
            VkClearValue clear{}; clear.depthStencil = {depth.clear, 0}; clears.push_back(clear);
        }
        // Complete validation and reserve every retention slot before changing recording state.
        record->uses.reserve(record->uses.size() + colors.size()); record->passes.reserve(record->passes.size() + 1);
        auto pass = std::make_unique<NativePass>(); pass->native = owner->native;
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<std::uint32_t>(references.size()); subpass.pColorAttachments = references.data();
        if (description.depth.texture) subpass.pDepthStencilAttachment = &depthReference;
        const auto dependencies = passDependencies();
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = static_cast<std::uint32_t>(attachments.size()); info.pAttachments = attachments.data();
        info.subpassCount = 1; info.pSubpasses = &subpass; info.dependencyCount = 2; info.pDependencies = dependencies.data();
        if (auto ok = owner->accept(owner->native->CreateRenderPass(owner->native->handle, &info, nullptr, &pass->pass), "vkCreateRenderPass"); !ok) return ok;
        const auto desc = colors.front()->textureDesc;
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = pass->pass; framebuffer.attachmentCount = static_cast<std::uint32_t>(views.size()); framebuffer.pAttachments = views.data();
        framebuffer.width = desc.width; framebuffer.height = desc.height; framebuffer.layers = 1;
        if (auto ok = owner->accept(owner->native->CreateFramebuffer(owner->native->handle, &framebuffer, nullptr, &pass->framebuffer), "vkCreateFramebuffer"); !ok) return ok;
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; begin.renderPass = pass->pass; begin.framebuffer = pass->framebuffer;
        begin.renderArea.extent = {desc.width, desc.height}; begin.clearValueCount = static_cast<std::uint32_t>(clears.size()); begin.pClearValues = clears.data();
        for (const auto& a : colors) retain(a).contentsInitialized = true;
        record->passes.push_back(std::move(pass));
        record->passDepth = bool(description.depth.texture); record->depthReadOnly = description.depth.readOnly;
        record->passColors = std::move(formats); record->width = desc.width; record->height = desc.height; record->selectedPipeline.reset(); record->selectedBindings = {};
        owner->native->CmdBeginRenderPass(record->command, &begin, VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{0, float(desc.height), float(desc.width), -float(desc.height), 0, 1};
        const VkRect2D scissor{{0, 0}, {desc.width, desc.height}};
        owner->native->CmdSetViewport(record->command, 0, 1, &viewport); owner->native->CmdSetScissor(record->command, 0, 1, &scissor);
        record->inPass = true; return {};
    }
    Result<void> endRenderPass() override {
        if (auto ok = ready(); !ok) return ok;
        if (!record->inPass) return fail(ErrorCode::InvalidState, "No active render pass");
        owner->native->CmdEndRenderPass(record->command); record->inPass = false; return {};
    }
    Result<void> setPipeline(const PipelinePtr& pipeline) override {
        if (auto ok = ready(); !ok) return ok;
        if (!record->inPass) return fail(ErrorCode::InvalidState, "Pipeline binding requires an active render pass");
        auto value = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        if (!value) return fail(pipeline ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument, "Expected a Vulkan graphics pipeline");
        if (value->owner != owner) return fail(ErrorCode::WrongDevice, "Pipeline belongs to another generation");
        if (value->native->colors != record->passColors || value->native->depth.has_value() != record->passDepth) return fail(ErrorCode::InvalidArgument, "Pipeline outputs do not match active pass");
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
    Result<void> bindResources(const ResourceBindings& bindings) override {
        if (auto ok = ready(); !ok) return ok;
        const auto p = record->selectedPipeline;
        if (!p) return fail(ErrorCode::InvalidState, "Resource binding requires a pipeline");
        if (p->descriptorSize > 256 - record->descriptors || record->snapshots.size() >= 256) return fail(ErrorCode::InvalidState, "Command descriptor capacity exhausted; split command list");
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
            auto at = slot(b.binding, b.element, BindingType::Texture2D); if (!at) return std::unexpected(at.error());
            auto a = texture(b.texture); if (!a) return std::unexpected(a.error());
            if ((*a)->textureDesc.depthStencil) return fail(ErrorCode::InvalidArgument, "Depth texture sampling unsupported");
            if (shadow(*a) != ResourceState::ShaderRead) return fail(ErrorCode::InvalidState, "Texture binding requires ShaderRead");
            const auto use = find(*a);
            if (!(use ? use->contentsInitialized : (*a)->contentsInitialized)) return fail(ErrorCode::InvalidState, "Sampled texture must have initialized contents");
            if (p->linear) {
                VkFormatProperties properties{};
                owner->native->instance->GetPhysicalDeviceFormatProperties(owner->physical, (*a)->textureDesc.format == Format::RGBA8UNorm ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT, &properties);
                if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) return fail(ErrorCode::Unsupported, "Texture lacks linear filtering support");
            }
            snapshot->resources[*at] = *a;
            images[*at] = {VK_NULL_HANDLE, (*a)->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
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
            for (const auto& e : p->bindings.entries) counts[e.type == BindingType::Texture2D ? 0 : 1] += e.count;
            counts[2] = static_cast<unsigned>(p->samplers.size());
            std::vector<VkDescriptorPoolSize> sizes;
            const VkDescriptorType types[]{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLER};
            for (unsigned i = 0; i < 3; ++i) if (counts[i]) sizes.push_back({types[i], counts[i]});
            VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool.maxSets = 1; pool.poolSizeCount = static_cast<unsigned>(sizes.size()); pool.pPoolSizes = sizes.data();
            if (auto ok = owner->accept(owner->native->CreateDescriptorPool(owner->native->handle, &pool, nullptr, &snapshot->pool), "vkCreateDescriptorPool"); !ok) return ok;
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; allocate.descriptorPool = snapshot->pool; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &p->setLayout;
            if (auto ok = owner->accept(owner->native->AllocateDescriptorSets(owner->native->handle, &allocate, &snapshot->set), "vkAllocateDescriptorSets"); !ok) return ok;
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
        if (!record->inPass || !record->selectedPipeline) return fail(ErrorCode::InvalidState, "Draw requires active pass and compatible pipeline");
        if (!vertices || !instances || vertices > UINT32_MAX - firstVertex || instances > UINT32_MAX - firstInstance)
            return fail(ErrorCode::InvalidArgument, "Invalid draw count/index range");
        if (auto ok = inputReady(vertices, instances, firstVertex, firstInstance); !ok) return ok;
        if (auto ok = bindingsReady(); !ok) return ok;
        owner->native->CmdDraw(record->command, vertices, instances, firstVertex, firstInstance); return {};
    }
    Result<void> drawIndexed(std::uint32_t count, std::uint32_t instances, std::uint32_t firstIndex, std::int32_t baseVertex, std::uint32_t firstInstance) override {
        if (auto ok = ready(); !ok) return ok;
        if (!record->inPass || !record->selectedPipeline) return fail(ErrorCode::InvalidState, "Indexed draw requires active pass and pipeline");
        if (!count || !instances || instances > UINT32_MAX - firstInstance) return fail(ErrorCode::InvalidArgument, "Invalid indexed draw counts");
        const auto& view = record->indices;
        if (!view.allocation || !indexState(shadow(view.allocation))) return fail(ErrorCode::InvalidState, "Missing/stale index binding");
        if (std::uint64_t(firstIndex) + count > view.bytes / view.element) return fail(ErrorCode::InvalidArgument, "Index range exceeds view");
        if (auto ok = inputReady(0, instances, 0, firstInstance, true, baseVertex); !ok) return ok;
        if (auto ok = bindingsReady(); !ok) return ok;
        owner->native->CmdDrawIndexed(record->command, count, instances, firstIndex, baseVertex, firstInstance); return {};
    }
    Result<void> writeTimestamp(std::uint32_t index) override {
        if (auto ok = ready(); !ok) return ok;
        if (!record->queries) return fail(ErrorCode::Unsupported, "Queue timestamps unsupported");
        if (index > 1 || record->timestampWritten[index] || record->timestampsResolved) return fail(ErrorCode::InvalidArgument, "Timestamp slot invalid or already written");
        if (!record->timestampWritten[0] && !record->timestampWritten[1]) record->timestampStartFirst = index == 0;
        owner->native->CmdWriteTimestamp(record->command, index ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, record->queries, index);
        record->timestampWritten[index] = true; return {};
    }
    Result<void> resolveTimestamps() override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (!record->queries) return fail(ErrorCode::Unsupported, "Queue timestamps unsupported");
        if (!record->timestampWritten[0] || !record->timestampWritten[1] || record->timestampsResolved) return fail(ErrorCode::InvalidState, "Two timestamps must be written once before resolving");
        // Vulkan copies query results directly to host memory after the submission fence.
        record->timestampsResolved = true; return {};
    }
    Result<double> elapsedGpuMilliseconds() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (!vulkan::timestampSupported(owner->timestampBits, owner->limits.timestampPeriod)) return fail(ErrorCode::Unsupported, "Queue timestamps unsupported");
        if (!record || !record->submitted || record->submitted > owner->completed || !record->timestampsResolved) return fail(ErrorCode::InvalidState, "GPU timestamps are not ready");
        if (record->timestampResult) return *record->timestampResult;
        struct Query { std::uint64_t ticks{}, available{}; }; std::array<Query, 2> results;
        const auto r = owner->native->GetQueryPoolResults(owner->native->handle, record->queries, 0, 2, sizeof(results), results.data(), sizeof(Query), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (r == VK_NOT_READY) return fail(ErrorCode::InvalidState, "GPU query results unavailable");
        if (auto ok = owner->accept(r, "vkGetQueryPoolResults"); !ok) return std::unexpected(ok.error());
        if (!results[0].available || !results[1].available) return fail(ErrorCode::InvalidState, "GPU query availability missing");
        const auto host = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - record->submittedAt).count();
        auto elapsed = vulkan::timestampMilliseconds(results[0].ticks, results[1].ticks, owner->timestampBits, owner->limits.timestampPeriod, host, record->timestampStartFirst);
        if (!elapsed) return std::unexpected(elapsed.error());
        record->timestampResult = *elapsed; return *elapsed;
    }
    Result<void> debugMarker(std::string_view) override { return unsupported(); }
};
struct VulkanQueue final : CommandQueue {
    std::shared_ptr<State> owner;
    Result<Fence> submit(CommandList& value) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        auto* commands = dynamic_cast<VulkanCommands*>(&value);
        if (!commands || commands->owner != owner) return fail(ErrorCode::WrongDevice, "Command list belongs to another device");
        auto record = commands->record;
        if (!record || !record->ended || record->submitted) return fail(ErrorCode::InvalidState, "Command list must be ended and unsubmitted");
        if (owner->last == UINT64_MAX) return fail(ErrorCode::InvalidState, "Fence sequence exhausted");
        for (const auto& use : record->uses)
            if (use.version != use.allocation->version || use.allocation->version == UINT64_MAX || !currentBackbuffer(*use.allocation))
                return fail(ErrorCode::InvalidState, "Stale command resource version");
        // Allocate the retention node before native submission; no throwing work follows success.
        auto& pending = owner->pending.emplace_back(Pending{{}, owner->last + 1, record});
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        auto r = owner->native->CreateFence(owner->native->handle, &fence, nullptr, &pending.fence);
        if (r != VK_SUCCESS) { owner->pending.pop_back(); return nativeError(r, "vkCreateFence"); }
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO}; info.commandBufferCount = 1; info.pCommandBuffers = &record->command;
        const auto submittedAt = std::chrono::steady_clock::now();
        r = owner->native->QueueSubmit(owner->native->queue, 1, &info, pending.fence);
        if (r != VK_SUCCESS) {
            owner->native->DestroyFence(owner->native->handle, pending.fence, nullptr); owner->pending.pop_back();
            if (auto ok = owner->accept(r, "vkQueueSubmit"); !ok) return std::unexpected(ok.error());
        }
        record->submitted = ++owner->last;
        record->submittedAt = submittedAt;
        for (auto& use : record->uses) {
            use.allocation->state = use.shadow; use.allocation->imageInitialized = use.imageInitialized;
            use.allocation->contentsInitialized = use.contentsInitialized;
            use.allocation->lastUse = owner->last; ++use.allocation->version;
        }
        return Fence{owner->last, owner->id};
    }
    Result<void> wait(Fence target, std::chrono::milliseconds timeout) override {
        if (auto ok = owner->check(); !ok) return ok;
        if (timeout.count() < 0 || timeout.count() > 4294967294ll) return fail(ErrorCode::InvalidArgument, "Fence timeout outside finite range");
        if (target.device != owner->id) return fail(ErrorCode::WrongDevice, "Fence belongs to another generation");
        if (!target.value || target.value > owner->last) return fail(ErrorCode::InvalidArgument, "Fence value was not submitted");
        if (auto ok = owner->poll(); !ok) return ok;
        if (target.value <= owner->completed) return {};
        auto found = std::find_if(owner->pending.begin(), owner->pending.end(), [&](auto& p) { return p.serial == target.value; });
        if (found == owner->pending.end()) return fail(ErrorCode::InvalidState, "Pending fence missing");
        if (auto ok = owner->accept(owner->native->WaitForFences(owner->native->handle, 1, &found->fence, VK_TRUE,
            static_cast<std::uint64_t>(timeout.count()) * 1000000ull), "vkWaitForFences"); !ok) return ok;
        return owner->poll();
    }
    Result<void> waitIdle() override {
        if (auto ok = owner->check(); !ok) return ok;
        if (auto ok = owner->accept(owner->native->QueueWaitIdle(owner->native->queue), "vkQueueWaitIdle"); !ok) return ok;
        return owner->poll();
    }
    Result<std::uint64_t> completedValue() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        return owner->completed;
    }
};
#include "Presentation.hpp"
struct VulkanDevice final : Device {
    std::shared_ptr<State> owner;
    DeviceCaps capabilities;
    VulkanQueue commands;
    ~VulkanDevice() override { owner->retire(); }
    const DeviceCaps& caps() const override { return capabilities; }
    Result<void> checkStatus() override { return owner->poll(); }
    Result<ResourceStatistics> resourceStatistics() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        ResourceStatistics result;
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
        std::erase_if(owner->recordings, [](auto& w) { return w.expired(); });
        std::erase_if(owner->pipelines, [](auto& w) { return w.expired(); });
        result.trackedObjects = owner->objects.size();
        for (auto& w : owner->allocations) if (auto a = w.lock()) { ++result.uniqueResources; result.resourceAllocationBytes += a->bytes; }
        result.pendingSubmissions = owner->pending.size();
        for (auto& p : owner->pending) result.pendingObjectReferences += 1 + p.recording->uses.size() + p.recording->passes.size() + p.recording->pipelines.size() + (p.recording->queries ? 1 : 0);
        return result;
    }
    Result<MemoryBudget> queryMemoryBudget(MemorySegment) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return fail(ErrorCode::Unsupported, "Vulkan heap sizes do not provide the OS process budget contract");
    }
    Result<std::unique_ptr<Device>> recover() override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return fail(ErrorCode::Unsupported, "Vulkan recovery unsupported");
    }
    Result<void> removeForTesting() override {
        if (auto ok = owner->check(); !ok) return ok;
        return fail(ErrorCode::Unsupported, "Vulkan loss injection unsupported");
    }
    Result<BufferPtr> createBuffer(BufferDesc desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (!desc.size || desc.size > SIZE_MAX || (desc.memory != Memory::Device && desc.memory != Memory::Upload && desc.memory != Memory::Readback))
            return fail(ErrorCode::InvalidArgument, "Invalid Vulkan buffer description");
        auto a = std::make_shared<Allocation>(); a->native = owner->native; a->desc = desc;
        a->state = desc.memory == Memory::Upload ? ResourceState::CopySource : desc.memory == Memory::Readback ? ResourceState::CopyDestination : ResourceState::Common;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size = desc.size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (auto ok = owner->accept(owner->native->CreateBuffer(owner->native->handle, &info, nullptr, &a->buffer), "vkCreateBuffer"); !ok) return std::unexpected(ok.error());
        VkMemoryRequirements req{}; owner->native->GetBufferMemoryRequirements(owner->native->handle, a->buffer, &req);
        std::array<vulkan::BufferMemoryType, VK_MAX_MEMORY_TYPES> types{};
        for (std::uint32_t i = 0; i < owner->memory.memoryTypeCount; ++i) {
            auto flags = owner->memory.memoryTypes[i].propertyFlags;
            types[i] = {bool(flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), bool(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT),
                bool(flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), bool(flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)};
        }
        auto type = vulkan::selectBufferMemoryType(std::span(types).first(owner->memory.memoryTypeCount), req.memoryTypeBits, desc.memory);
        if (!type) return fail(ErrorCode::Unsupported, "No compatible Vulkan buffer memory type");
        a->bytes = req.size; a->coherent = (owner->memory.memoryTypes[*type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocate.allocationSize = req.size; allocate.memoryTypeIndex = *type;
        if (auto ok = owner->accept(owner->native->AllocateMemory(owner->native->handle, &allocate, nullptr, &a->memory), "vkAllocateMemory"); !ok) return std::unexpected(ok.error());
        if (auto ok = owner->accept(owner->native->BindBufferMemory(owner->native->handle, a->buffer, a->memory, 0), "vkBindBufferMemory"); !ok) return std::unexpected(ok.error());
        auto value = std::make_shared<VulkanBuffer>(); value->owner = owner; value->allocation = a; value->token = std::make_shared<int>();
        std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->allocations.push_back(a); owner->objects.push_back(value->token); return BufferPtr(value);
    }
    Result<TexturePtr> createTexture(TextureDesc desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (auto ok = validateTextureDescription(desc); !ok) return std::unexpected(ok.error());
        if ((desc.renderTarget || desc.depthStencil) && (desc.width > owner->maxFramebufferWidth || desc.height > owner->maxFramebufferHeight))
            return fail(ErrorCode::Unsupported, "Color attachment exceeds adapter framebuffer extent limits");
        const auto format = textureFormat(desc.format);
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            (desc.depthStencil ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_SAMPLED_BIT | (desc.renderTarget ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0));
        VkImageFormatProperties properties{};
        const auto r = owner->native->instance->GetPhysicalDeviceImageFormatProperties(owner->physical, format,
            VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &properties);
        if (r == VK_ERROR_FORMAT_NOT_SUPPORTED) return fail(ErrorCode::Unsupported, "Vulkan color image format unsupported", r);
        if (auto ok = owner->accept(r, "vkGetPhysicalDeviceImageFormatProperties"); !ok) return std::unexpected(ok.error());
        if (desc.width > properties.maxExtent.width || desc.height > properties.maxExtent.height || !(properties.sampleCounts & VK_SAMPLE_COUNT_1_BIT))
            return fail(ErrorCode::Unsupported, "Vulkan image exceeds adapter format limits");
        auto a = std::make_shared<Allocation>(); a->native = owner->native; a->textureDesc = desc;
        const auto rowBytes = desc.width * texelBytes(desc.format);
        const auto rowPitch = (rowBytes + 255u) & ~255u;
        a->footprint = {std::uint64_t(rowPitch) * desc.height, rowPitch, rowBytes, desc.height};
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D; info.format = format; info.extent = {desc.width, desc.height, 1};
        info.mipLevels = info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE; info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (auto ok = owner->accept(owner->native->CreateImage(owner->native->handle, &info, nullptr, &a->image), "vkCreateImage"); !ok) return std::unexpected(ok.error());
        VkMemoryRequirements req{}; owner->native->GetImageMemoryRequirements(owner->native->handle, a->image, &req);
        std::optional<std::uint32_t> type;
        for (std::uint32_t n = 0; n < owner->memory.memoryTypeCount; ++n)
            if ((req.memoryTypeBits & (1u << n)) && (owner->memory.memoryTypes[n].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { type = n; break; }
        if (!type) return fail(ErrorCode::Unsupported, "No device-local Vulkan image memory type");
        a->bytes = req.size;
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocate.allocationSize = req.size; allocate.memoryTypeIndex = *type;
        if (auto ok = owner->accept(owner->native->AllocateMemory(owner->native->handle, &allocate, nullptr, &a->memory), "vkAllocateMemory image"); !ok) return std::unexpected(ok.error());
        if (auto ok = owner->accept(owner->native->BindImageMemory(owner->native->handle, a->image, a->memory, 0), "vkBindImageMemory"); !ok) return std::unexpected(ok.error());
        {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = a->image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
            view.subresourceRange = {imageAspect(*a), 0, 1, 0, 1};
            if (auto ok = owner->accept(owner->native->CreateImageView(owner->native->handle, &view, nullptr, &a->view), "vkCreateImageView"); !ok) return std::unexpected(ok.error());
        }
        auto value = std::make_shared<VulkanTexture>(); value->owner = owner; value->allocation = a; value->token = std::make_shared<int>();
        std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->allocations.push_back(a); owner->objects.push_back(value->token); return TexturePtr(value);
    }
    Result<PipelinePtr> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        const auto targets = desc.renderTargets.value_or(RenderTargetLayout{{desc.colorFormat}, {}});
        const bool hasFragment = !desc.fragment.bytes.empty();
        if (desc.vertex.format != ShaderFormat::SPIRV || (hasFragment && desc.fragment.format != ShaderFormat::SPIRV))
            return fail(ErrorCode::Unsupported, "Vulkan requires explicit SPIRV shader format");
        if (auto ok = validateRenderTargets(targets, desc.depthState); !ok) return std::unexpected(ok.error());
        if (!hasFragment && !targets.colors.empty()) return fail(ErrorCode::InvalidArgument, "Color outputs require a fragment shader");
        if (targets.colors.size() > owner->maxColorAttachments) return fail(ErrorCode::Unsupported, "Color outputs exceed adapter limit");
        if (targets.depth) {
            VkFormatProperties properties{}; owner->native->instance->GetPhysicalDeviceFormatProperties(owner->physical, VK_FORMAT_D32_SFLOAT, &properties);
            if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) return fail(ErrorCode::Unsupported, "Adapter lacks D32 depth attachment support");
        }
        if (desc.sampledTexture && (!desc.bindingLayout.entries.empty() || !desc.bindingLayout.samplers.empty())) return fail(ErrorCode::InvalidArgument, "Legacy sampledTexture and explicit layout cannot be combined");
        const auto resourceLayout = desc.sampledTexture ? BindingLayout{{{}}, {{}}} : desc.bindingLayout;
        if (auto ok = validateBindingLayout(resourceLayout); !ok) return std::unexpected(ok.error());
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
                format == Format::RGBA8UNorm ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT, &properties);
            const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | (desc.alphaBlend ? VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT : 0);
            if ((properties.optimalTilingFeatures & required) != required) return fail(ErrorCode::Unsupported, "Adapter format lacks attachment/blending support");
        }
        auto p = std::make_shared<NativePipeline>(); p->native = owner->native; p->colors = targets.colors; p->depth = targets.depth; p->depthState = desc.depthState;
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
            count(e.type == BindingType::Texture2D ? 0 : 1, e.count, e.visibility);
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
            VkSampler sampler{};
            if (auto ok = owner->accept(owner->native->CreateSampler(owner->native->handle, &info, nullptr, &sampler), "vkCreateSampler"); !ok) return std::unexpected(ok.error());
            p->samplers.push_back(sampler); p->linear |= s.filter == SamplerFilter::Linear;
        }
        std::vector<VkDescriptorSetLayoutBinding> resources;
        for (unsigned i = 0; i < resourceLayout.entries.size(); ++i) {
            const auto& e = resourceLayout.entries[i]; resources.push_back({i, e.type == BindingType::Texture2D ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, e.count, stages(e.visibility), nullptr});
        }
        for (unsigned i = 0; i < p->samplers.size(); ++i) resources.push_back({32 + i, VK_DESCRIPTOR_TYPE_SAMPLER, 1, stages(resourceLayout.samplers[i].visibility), &p->samplers[i]});
        if (!resources.empty()) {
            VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; info.bindingCount = static_cast<unsigned>(resources.size()); info.pBindings = resources.data();
            if (auto ok = owner->accept(owner->native->CreateDescriptorSetLayout(owner->native->handle, &info, nullptr, &p->setLayout), "vkCreateDescriptorSetLayout"); !ok) return std::unexpected(ok.error());
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
            return owner->accept(owner->native->CreateShaderModule(owner->native->handle, &info, nullptr, &result), "vkCreateShaderModule");
        };
        if (auto ok = module(*vertex, modules.vertex); !ok) return std::unexpected(ok.error());
        if (auto ok = hasFragment ? module(*fragment, modules.fragment) : Result<void>{}; !ok) return std::unexpected(ok.error());
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        if (p->setLayout) { layout.setLayoutCount = 1; layout.pSetLayouts = &p->setLayout; }
        if (auto ok = owner->accept(owner->native->CreatePipelineLayout(owner->native->handle, &layout, nullptr, &p->layout), "vkCreatePipelineLayout"); !ok) return std::unexpected(ok.error());
        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> references;
        for (auto format : targets.colors) {
            VkAttachmentDescription a{}; a.format = format == Format::RGBA8UNorm ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT;
            a.samples = VK_SAMPLE_COUNT_1_BIT; a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            references.push_back({static_cast<std::uint32_t>(attachments.size()), a.initialLayout}); attachments.push_back(a);
        }
        VkAttachmentReference depthReference{};
        if (targets.depth) {
            VkAttachmentDescription a{}; a.format = VK_FORMAT_D32_SFLOAT; a.samples = VK_SAMPLE_COUNT_1_BIT;
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
        if (auto ok = owner->accept(owner->native->CreateRenderPass(owner->native->handle, &pass, nullptr, &p->pass), "vkCreateRenderPass pipeline"); !ok) return std::unexpected(ok.error());
        VkPipelineShaderStageCreateInfo shaderStages[2]{};
        for (auto& s : shaderStages) { s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; s.pName = "main"; }
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; shaderStages[0].module = modules.vertex; shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; shaderStages[1].module = modules.fragment;
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size()); input.pVertexBindingDescriptions = bindings.data();
        input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()); input.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.frontFace = VK_FRONT_FACE_CLOCKWISE; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
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
        if (auto ok = owner->accept(owner->native->CreateGraphicsPipelines(owner->native->handle, {}, 1, &info, nullptr, &p->pipeline), "vkCreateGraphicsPipelines"); !ok) return std::unexpected(ok.error());
        auto value = std::make_shared<VulkanPipeline>(); value->owner = owner; value->native = p; value->token = std::make_shared<int>();
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); }); std::erase_if(owner->pipelines, [](auto& w) { return w.expired(); });
        owner->pipelines.push_back(p); owner->objects.push_back(value->token); return PipelinePtr(value);
    }
    Result<std::unique_ptr<CommandList>> createCommandList() override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        auto value = std::make_unique<VulkanCommands>(); value->owner = owner; value->token = std::make_shared<int>();
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->objects.push_back(value->token); return std::unique_ptr<CommandList>(std::move(value));
    }
    Result<std::unique_ptr<SwapChain>> createSwapChain(Surface surface, SwapChainDesc desc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return makeSwapchain(owner, surface, desc);
    }
    CommandQueue& queue() override { return commands; }
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
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = selected->family; queue.queueCount = 1; queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; info.queueCreateInfoCount = 1; info.pQueueCreateInfos = &queue;
    std::uint32_t extensionCount{};
    auto er = (*instance)->EnumerateDeviceExtensionProperties(selected->physical, nullptr, &extensionCount, nullptr);
    if (er != VK_SUCCESS) return nativeError(er, "Enumerate device extensions");
    std::vector<VkExtensionProperties> extensions(extensionCount);
    er = (*instance)->EnumerateDeviceExtensionProperties(selected->physical, nullptr, &extensionCount, extensions.data());
    if (er != VK_SUCCESS) return nativeError(er, "Enumerate device extensions");
    extensions.resize(extensionCount);
    const auto hasExtension = [&](const char* name) { return std::any_of(extensions.begin(), extensions.end(), [&](auto& p) { return std::strcmp(p.extensionName, name) == 0; }); };
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT};
    if ((*instance)->presentation && hasExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME) && hasExtension(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) {
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; features.pNext = &maintenance;
        (*instance)->GetPhysicalDeviceFeatures2(selected->physical, &features);
        native->presentation = maintenance.swapchainMaintenance1 == VK_TRUE;
    }
    const char* swapchainExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME};
    if (native->presentation) { info.enabledExtensionCount = 2; info.ppEnabledExtensionNames = swapchainExtensions; info.pNext = &maintenance; }
    VkPhysicalDeviceFeatures enabled{}; enabled.fullDrawIndexUint32 = selected->features.fullDrawIndexUint32; info.pEnabledFeatures = &enabled;
    if (auto r = (*instance)->CreateDevice(selected->physical, &info, nullptr, &native->handle); r != VK_SUCCESS) return nativeError(r, "vkCreateDevice");
#define LOAD(name) native->name = reinterpret_cast<PFN_vk##name>((*instance)->GetDeviceProcAddr(native->handle, "vk" #name)); \
    if (!native->name) return fail(ErrorCode::Unsupported, "Vulkan device entry point missing: vk" #name);
    DEVICE_FUNCTIONS(LOAD)
#undef LOAD
    if (native->presentation) {
#define LOAD_WSI(name) native->name = reinterpret_cast<PFN_vk##name>((*instance)->GetDeviceProcAddr(native->handle, "vk" #name)); if (!native->name) return fail(ErrorCode::Unsupported, "Vulkan swapchain entry point missing: vk" #name);
        LOAD_WSI(CreateSwapchainKHR) LOAD_WSI(DestroySwapchainKHR) LOAD_WSI(GetSwapchainImagesKHR) LOAD_WSI(AcquireNextImageKHR) LOAD_WSI(QueuePresentKHR)
#undef LOAD_WSI
    }
    native->GetDeviceQueue(native->handle, selected->family, 0, &native->queue);
    auto owner = std::make_shared<State>(); owner->native = native; owner->physical = selected->physical;
    owner->memory = selected->memory; owner->family = selected->family;
    owner->timestampBits = selected->timestampBits;
    owner->maxColorAttachments = selected->maxColorAttachments;
    owner->maxFramebufferWidth = selected->maxFramebufferWidth; owner->maxFramebufferHeight = selected->maxFramebufferHeight;
    owner->limits = selected->limits; owner->fullDrawIndexUint32 = enabled.fullDrawIndexUint32 != 0;
    auto device = std::make_unique<VulkanDevice>(); device->owner = owner; device->commands.owner = owner;
    device->capabilities.backend = Backend::Vulkan; device->capabilities.adapterName = selected->info.name;
    device->capabilities.softwareAdapter = selected->info.softwareAdapter; device->capabilities.adapterId = selected->info.id;
    device->capabilities.multipleRenderTargets = selected->maxColorAttachments >= 2;
    device->capabilities.vertexInput = device->capabilities.indexedDrawing = true;
    device->capabilities.timestampQueries = vulkan::timestampSupported(owner->timestampBits, owner->limits.timestampPeriod);
    device->capabilities.swapChain = native->presentation;
    device->capabilities.resourceBinding = device->capabilities.bindingLayouts = true;
    VkImageFormatProperties depthProperties{};
    device->capabilities.depthAttachment = owner->native->instance->GetPhysicalDeviceImageFormatProperties(owner->physical, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0, &depthProperties) == VK_SUCCESS && (depthProperties.sampleCounts & VK_SAMPLE_COUNT_1_BIT);
    return std::unique_ptr<Device>(std::move(device));
}
} // namespace gfx
