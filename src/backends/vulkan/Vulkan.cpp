#include <graphics/Graphics.hpp>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <list>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
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
    X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceQueueFamilyProperties) X(CreateDevice) X(GetDeviceProcAddr)
struct Instance {
    std::shared_ptr<Loader> loader;
    VkInstance handle{};
    VkDebugUtilsMessengerEXT messenger{};
    PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger{};
    std::atomic<std::uint64_t> errors{};
#define DECLARE(name) PFN_vk##name name{};
    INSTANCE_FUNCTIONS(DECLARE)
#undef DECLARE
    ~Instance() {
        if (messenger) destroyMessenger(handle, messenger, nullptr);
        if (handle && DestroyInstance) DestroyInstance(handle, nullptr);
    }
};
VKAPI_ATTR VkBool32 VKAPI_CALL validationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT*, void* user) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        static_cast<Instance*>(user)->errors.fetch_add(1, std::memory_order_relaxed);
    return VK_FALSE;
}
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
    const char* layer = "VK_LAYER_KHRONOS_validation";
    const char* extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
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
        r = extensions(nullptr, &count, nullptr);
        if (r != VK_SUCCESS) return nativeError(r, "Enumerate debug extensions");
        std::vector<VkExtensionProperties> names(count);
        r = extensions(nullptr, &count, names.data());
        if (r != VK_SUCCESS) return nativeError(r, "Enumerate debug extensions");
        names.resize(count);
        if (std::none_of(names.begin(), names.end(), [&](auto& p) { return std::strcmp(p.extensionName, extension) == 0; }))
            return fail(ErrorCode::Unsupported, "VK_EXT_debug_utils is unavailable");
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "GraphicsSDK"; app.apiVersion = VK_API_VERSION_1_1;
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = validationMessage; debug.pUserData = instance.get();
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    if (validation) { info.enabledLayerCount = 1; info.ppEnabledLayerNames = &layer; info.enabledExtensionCount = 1; info.ppEnabledExtensionNames = &extension; info.pNext = &debug; }
    if (auto r = create(&info, nullptr, &instance->handle); r != VK_SUCCESS) return nativeError(r, "vkCreateInstance");
#define LOAD(name) instance->name = reinterpret_cast<PFN_vk##name>(get(instance->handle, "vk" #name)); \
    if (!instance->name) return fail(ErrorCode::Unsupported, "Vulkan instance entry point missing: vk" #name);
    INSTANCE_FUNCTIONS(LOAD)
#undef LOAD
    if (validation) {
        const auto make = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(get(instance->handle, "vkCreateDebugUtilsMessengerEXT"));
        instance->destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(get(instance->handle, "vkDestroyDebugUtilsMessengerEXT"));
        if (!make || !instance->destroyMessenger) return fail(ErrorCode::Unsupported, "Vulkan debug messenger unavailable");
        if (auto r = make(instance->handle, &debug, nullptr, &instance->messenger); r != VK_SUCCESS) return nativeError(r, "Create validation messenger");
    }
    return instance;
}
struct Candidate {
    AdapterInfo info;
    VkPhysicalDevice physical{};
    VkPhysicalDeviceMemoryProperties memory{};
    std::uint32_t family{};
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
    X(MapMemory) X(UnmapMemory) X(FlushMappedMemoryRanges) X(InvalidateMappedMemoryRanges) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(CmdCopyBuffer) X(CmdPipelineBarrier) X(CreateFence) X(DestroyFence) X(GetFenceStatus) X(WaitForFences)
struct NativeDevice {
    std::shared_ptr<Instance> instance;
    VkDevice handle{};
    VkQueue queue{};
#define DECLARE(name) PFN_vk##name name{};
    DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
    void release() { if (handle) { DestroyDevice(handle, nullptr); handle = {}; } }
    ~NativeDevice() { if (handle && DestroyDevice) release(); }
};
struct Allocation {
    std::shared_ptr<NativeDevice> native;
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkDeviceSize bytes{};
    bool coherent{};
    BufferDesc desc;
    ResourceState state{ResourceState::Common};
    std::uint64_t version{}, lastUse{};
    void release() {
        if (buffer) native->DestroyBuffer(native->handle, buffer, nullptr);
        if (memory) native->FreeMemory(native->handle, memory, nullptr);
        buffer = {}; memory = {};
    }
    ~Allocation() { release(); }
};
struct Use { std::shared_ptr<Allocation> allocation; std::uint64_t version; ResourceState shadow; };
struct Recording {
    std::shared_ptr<NativeDevice> native;
    VkCommandPool pool{};
    VkCommandBuffer command{};
    bool open{}, ended{};
    std::uint64_t submitted{};
    std::vector<Use> uses;
    void release() {
        if (pool) native->DestroyCommandPool(native->handle, pool, nullptr);
        pool = {}; command = {}; uses.clear(); open = ended = false;
    }
    ~Recording() { release(); }
};
struct Pending { VkFence fence{}; std::uint64_t serial{}; std::shared_ptr<Recording> recording; };
std::atomic<std::uint64_t> generations{1};
struct State {
    std::shared_ptr<NativeDevice> native;
    VkPhysicalDeviceMemoryProperties memory{};
    std::uint32_t family{};
    std::thread::id thread{std::this_thread::get_id()};
    std::uint64_t id{generations.fetch_add(1)}, last{}, completed{};
    bool retired{}, lost{};
    std::list<Pending> pending;
    std::vector<std::weak_ptr<Allocation>> allocations;
    std::vector<std::weak_ptr<Recording>> recordings;
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
        for (auto& weak : allocations) if (auto value = weak.lock()) value->release();
        recordings.clear(); allocations.clear(); objects.clear();
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
VkAccessFlags access(ResourceState state) {
    if (state == ResourceState::CopySource) return VK_ACCESS_TRANSFER_READ_BIT;
    if (state == ResourceState::CopyDestination) return VK_ACCESS_TRANSFER_WRITE_BIT;
    return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
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
    Result<void> unsupported() const {
        if (auto ok = ready(); !ok) return ok;
        return fail(ErrorCode::Unsupported, "Vulkan backend currently supports buffer transfers only");
    }
    Result<std::shared_ptr<Allocation>> buffer(const BufferPtr& buffer) const {
        auto value = std::dynamic_pointer_cast<VulkanBuffer>(buffer);
        if (!value) return fail(buffer ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument, "Expected a Vulkan buffer");
        if (value->owner != owner) return fail(ErrorCode::WrongDevice, "Buffer belongs to another device generation");
        return value->allocation;
    }
    Use* find(const std::shared_ptr<Allocation>& a) {
        const auto it = std::find_if(record->uses.begin(), record->uses.end(), [&](auto& use) { return use.allocation == a; });
        return it == record->uses.end() ? nullptr : &*it;
    }
    ResourceState shadow(const std::shared_ptr<Allocation>& a) { auto use = find(a); return use ? use->shadow : a->state; }
    Use& retain(const std::shared_ptr<Allocation>& a) {
        if (auto use = find(a)) return *use;
        return record->uses.emplace_back(Use{a, a->version, a->state});
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
        next->open = true;
        std::erase_if(owner->recordings, [](auto& w) { return w.expired(); });
        owner->recordings.push_back(next); record = std::move(next); return {};
    }
    Result<void> end() override {
        if (auto ok = ready(); !ok) return ok;
        const auto r = owner->native->EndCommandBuffer(record->command);
        record->open = false; record->ended = r == VK_SUCCESS;
        return owner->accept(r, "vkEndCommandBuffer");
    }
    Result<void> transition(const BufferPtr& value, ResourceState target) override {
        if (auto ok = ready(); !ok) return ok;
        const auto a = buffer(value); if (!a) return std::unexpected(a.error());
        if (target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination)
            return fail(ErrorCode::Unsupported, "Vulkan transfer state unsupported");
        if (((*a)->desc.memory == Memory::Upload && target != ResourceState::CopySource) ||
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
        if (auto ok = ready(); !ok) return ok;
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
    Result<void> transition(const TexturePtr&, ResourceState) override { return unsupported(); }
    Result<void> copyBufferToTexture(const BufferPtr&, const TexturePtr&) override { return unsupported(); }
    Result<void> copyTextureToBuffer(const TexturePtr&, const BufferPtr&) override { return unsupported(); }
    Result<void> beginRenderPass(const TexturePtr&, Color) override { return unsupported(); }
    Result<void> beginRenderPass(const RenderPassDesc&) override { return unsupported(); }
    Result<void> endRenderPass() override { return unsupported(); }
    Result<void> setPipeline(const PipelinePtr&) override { return unsupported(); }
    Result<void> bindTexture(const TexturePtr&) override { return unsupported(); }
    Result<void> bindResources(const ResourceBindings&) override { return unsupported(); }
    Result<void> bindVertexBuffer(std::uint32_t, const BufferPtr&, std::uint32_t, std::uint64_t, std::uint64_t) override { return unsupported(); }
    Result<void> bindIndexBuffer(const BufferPtr&, IndexFormat, std::uint64_t, std::uint64_t) override { return unsupported(); }
    Result<void> setScissor(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { return unsupported(); }
    Result<void> draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { return unsupported(); }
    Result<void> drawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override { return unsupported(); }
    Result<void> writeTimestamp(std::uint32_t) override { return unsupported(); }
    Result<void> resolveTimestamps() override { return unsupported(); }
    Result<double> elapsedGpuMilliseconds() override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return fail(ErrorCode::Unsupported, "Vulkan timestamps unsupported");
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
            if (use.version != use.allocation->version || use.allocation->version == UINT64_MAX)
                return fail(ErrorCode::InvalidState, "Stale command resource version");
        // Allocate the retention node before native submission; no throwing work follows success.
        auto& pending = owner->pending.emplace_back(Pending{{}, owner->last + 1, record});
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        auto r = owner->native->CreateFence(owner->native->handle, &fence, nullptr, &pending.fence);
        if (r != VK_SUCCESS) { owner->pending.pop_back(); return nativeError(r, "vkCreateFence"); }
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO}; info.commandBufferCount = 1; info.pCommandBuffers = &record->command;
        r = owner->native->QueueSubmit(owner->native->queue, 1, &info, pending.fence);
        if (r != VK_SUCCESS) {
            owner->native->DestroyFence(owner->native->handle, pending.fence, nullptr); owner->pending.pop_back();
            if (auto ok = owner->accept(r, "vkQueueSubmit"); !ok) return std::unexpected(ok.error());
        }
        record->submitted = ++owner->last;
        for (auto& use : record->uses) { use.allocation->state = use.shadow; use.allocation->lastUse = owner->last; ++use.allocation->version; }
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
        result.trackedObjects = owner->objects.size();
        for (auto& w : owner->allocations) if (auto a = w.lock()) { ++result.uniqueResources; result.resourceAllocationBytes += a->bytes; }
        result.pendingSubmissions = owner->pending.size();
        for (auto& p : owner->pending) result.pendingObjectReferences += 1 + p.recording->uses.size();
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
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (auto ok = owner->accept(owner->native->CreateBuffer(owner->native->handle, &info, nullptr, &a->buffer), "vkCreateBuffer"); !ok) return std::unexpected(ok.error());
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
        if (auto ok = owner->accept(owner->native->AllocateMemory(owner->native->handle, &allocate, nullptr, &a->memory), "vkAllocateMemory"); !ok) return std::unexpected(ok.error());
        if (auto ok = owner->accept(owner->native->BindBufferMemory(owner->native->handle, a->buffer, a->memory, 0), "vkBindBufferMemory"); !ok) return std::unexpected(ok.error());
        auto value = std::make_shared<VulkanBuffer>(); value->owner = owner; value->allocation = a; value->token = std::make_shared<int>();
        std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->allocations.push_back(a); owner->objects.push_back(value->token); return BufferPtr(value);
    }
    Result<TexturePtr> createTexture(TextureDesc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return fail(ErrorCode::Unsupported, "Vulkan textures unsupported");
    }
    Result<PipelinePtr> createGraphicsPipeline(const GraphicsPipelineDesc&) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return fail(ErrorCode::Unsupported, "Vulkan graphics pipelines unsupported");
    }
    Result<std::unique_ptr<CommandList>> createCommandList() override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        auto value = std::make_unique<VulkanCommands>(); value->owner = owner; value->token = std::make_shared<int>();
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        owner->objects.push_back(value->token); return std::unique_ptr<CommandList>(std::move(value));
    }
    Result<std::unique_ptr<SwapChain>> createSwapChain(Surface, SwapChainDesc) override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return fail(ErrorCode::Unsupported, "Vulkan presentation unsupported");
    }
    CommandQueue& queue() override { return commands; }
    Result<std::uint64_t> validationErrorCount() override {
        if (std::this_thread::get_id() != owner->thread) return fail(ErrorCode::InvalidState, "Validation count requires creating thread");
        return owner->native->instance->errors.load(std::memory_order_relaxed);
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
    if (auto r = (*instance)->CreateDevice(selected->physical, &info, nullptr, &native->handle); r != VK_SUCCESS) return nativeError(r, "vkCreateDevice");
#define LOAD(name) native->name = reinterpret_cast<PFN_vk##name>((*instance)->GetDeviceProcAddr(native->handle, "vk" #name)); \
    if (!native->name) return fail(ErrorCode::Unsupported, "Vulkan device entry point missing: vk" #name);
    DEVICE_FUNCTIONS(LOAD)
#undef LOAD
    native->GetDeviceQueue(native->handle, selected->family, 0, &native->queue);
    auto owner = std::make_shared<State>(); owner->native = native; owner->memory = selected->memory; owner->family = selected->family;
    auto device = std::make_unique<VulkanDevice>(); device->owner = owner; device->commands.owner = owner;
    device->capabilities.backend = Backend::Vulkan; device->capabilities.adapterName = selected->info.name;
    device->capabilities.softwareAdapter = selected->info.softwareAdapter; device->capabilities.adapterId = selected->info.id;
    return std::unique_ptr<Device>(std::move(device));
}
} // namespace gfx
