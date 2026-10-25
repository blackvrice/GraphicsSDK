#include <graphics/Graphics.hpp>
#include "VulkanValidation.hpp"
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#include <link.h>
#endif

namespace {
unsigned assertions{};
void check(bool value, const char* text) { ++assertions; if (!value) throw std::runtime_error(text); }
void ok(VkResult r) { if (r != VK_SUCCESS) throw std::runtime_error("Native Vulkan result=" + std::to_string(r)); }
struct Observed {
    gfx::vulkan::ValidationCounter counter;
    std::atomic<unsigned> sizeErrors{}, syncErrors{}, injected{}, unexpected{};
    bool abortExpected{};
};
VKAPI_ATTR VkBool32 VKAPI_CALL observe(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    auto& o = *static_cast<Observed*>(user);
    gfx::vulkan::validationMessage(severity, type, data, &o.counter);
    if (!(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) return VK_FALSE;
    const char* id = data && data->pMessageIdName ? data->pMessageIdName : "";
    if (std::strcmp(id, "VUID-VkBufferCreateInfo-size-00912") == 0) ++o.sizeErrors;
    else if (std::strcmp(id, "SYNC-HAZARD-WRITE-AFTER-WRITE") == 0) ++o.syncErrors;
    else if (std::strcmp(id, "GFX-COUNTER-TEST") == 0) ++o.injected;
    else { ++o.unexpected; std::cerr << "Unexpected validation: " << id << " " << (data ? data->pMessage : "") << '\n'; }
    // Positive controls abort the invalid call in validation, before the driver.
    return o.abortExpected ? VK_TRUE : VK_FALSE;
}
#define INSTANCE(X) X(DestroyInstance) X(CreateDebugUtilsMessengerEXT) X(DestroyDebugUtilsMessengerEXT) X(SubmitDebugUtilsMessageEXT) \
    X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceQueueFamilyProperties) X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE(X) X(DestroyDevice) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdCopyBuffer)
struct Probe {
#ifdef _WIN32
    HMODULE library{LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    PFN_vkGetInstanceProcAddr get{library ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(library, "vkGetInstanceProcAddr")) : nullptr};
#else
    void* library{dlopen("libvulkan.so.1", RTLD_NOW|RTLD_LOCAL)};
    PFN_vkGetInstanceProcAddr get{library ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr")) : nullptr};
#endif
    Observed observed;
    VkInstance instance{}; VkDebugUtilsMessengerEXT messenger{}; VkDevice device{}; VkCommandPool pool{};
    VkBuffer buffers[2]{}; VkDeviceMemory allocations[2]{};
    VkPhysicalDeviceMemoryProperties memory{};
    VkPhysicalDeviceProperties properties{};
    unsigned physicalCount{}, family{};
#define DECLARE(n) PFN_vk##n n{};
    INSTANCE(DECLARE) DEVICE(DECLARE)
#undef DECLARE
    void close() {
        if (pool) DestroyCommandPool(device, pool, nullptr); pool = {};
        for (unsigned i = 0; i < 2; ++i) {
            if (buffers[i]) DestroyBuffer(device, buffers[i], nullptr);
            if (allocations[i]) FreeMemory(device, allocations[i], nullptr);
            buffers[i] = {}; allocations[i] = {};
        }
        if (device) DestroyDevice(device, nullptr); device = {};
        if (messenger) DestroyDebugUtilsMessengerEXT(instance, messenger, nullptr); messenger = {};
        if (instance) DestroyInstance(instance, nullptr); instance = {};
    }
    ~Probe() {
        close();
#ifdef _WIN32
        if (library) FreeLibrary(library);
#else
        if (library) dlclose(library);
#endif
    }
    void initialize(unsigned index, bool synchronization = true) {
        check(get != nullptr, "Native Vulkan loader");
        gfx::vulkan::ValidationConfiguration config(observed.counter);
        if (!synchronization) config.features.enabledValidationFeatureCount = 0;
        config.messenger.pfnUserCallback = observe; config.messenger.pUserData = &observed;
        // Also receive non-errors to verify that the shared counter ignores them.
        config.messenger.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo = &app;
        info.enabledLayerCount = 1; info.ppEnabledLayerNames = &config.layer;
        info.enabledExtensionCount = 3; info.ppEnabledExtensionNames = config.extensions; info.pNext = &config.layerSettings;
        auto create = reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance")); check(create != nullptr, "vkCreateInstance");
        ok(create(&info, nullptr, &instance));
#ifdef _WIN32
        const auto loaded = GetModuleHandleW(L"VkLayer_khronos_validation.dll");
        wchar_t path[32768]{};
        check(loaded && GetModuleFileNameW(loaded, path, 32768), "Actual validation DLL is loaded");
        std::cout << "loaded_layer=" << std::filesystem::path(path).generic_string() << std::endl;
        if (const auto expected = std::getenv("GFX_EXPECT_VULKAN_LAYER"))
            check(std::filesystem::equivalent(path, expected), "Loaded validation DLL matches frozen expected runtime");
#endif
        #ifndef _WIN32
        std::vector<std::filesystem::path> layers;
        dl_iterate_phdr([](dl_phdr_info* module, std::size_t, void* user) {
            if (module->dlpi_name && std::filesystem::path(module->dlpi_name).filename() == "libVkLayer_khronos_validation.so")
                static_cast<std::vector<std::filesystem::path>*>(user)->emplace_back(module->dlpi_name);
            return 0;
        }, &layers);
        check(layers.size() == 1, "Exactly one native validation shared library is loaded");
        std::cout << "loaded_layer=" << std::filesystem::canonical(layers.front()).generic_string() << std::endl;
        if (const auto expected = std::getenv("GFX_EXPECT_VULKAN_LAYER"))
            check(std::filesystem::equivalent(layers.front(), expected), "Loaded validation shared library matches frozen expected runtime");
        #endif
#define LOAD(n) n = reinterpret_cast<PFN_vk##n>(get(instance, "vk" #n)); check(n != nullptr, "vk" #n);
        INSTANCE(LOAD)
#undef LOAD
        ok(CreateDebugUtilsMessengerEXT(instance, &config.messenger, nullptr, &messenger));
        ok(EnumeratePhysicalDevices(instance, &physicalCount, nullptr));
        std::vector<VkPhysicalDevice> all(physicalCount); ok(EnumeratePhysicalDevices(instance, &physicalCount, all.data()));
        check(index < physicalCount, "Native physical device exists"); auto physical = all[index];
        GetPhysicalDeviceProperties(physical, &properties); GetPhysicalDeviceMemoryProperties(physical, &memory);
        unsigned count{}; GetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count); GetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
        family = count;
        for (unsigned i = 0; i < count; ++i) if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { family = i; break; }
        check(family < count, "Native graphics queue required");
        float priority = 1;
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; q.queueFamilyIndex = family; q.queueCount = 1; q.pQueuePriorities = &priority;
        VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; d.queueCreateInfoCount = 1; d.pQueueCreateInfos = &q;
        ok(CreateDevice(physical, &d, nullptr, &device));
#define LOAD(n) n = reinterpret_cast<PFN_vk##n>(GetDeviceProcAddr(device, "vk" #n)); check(n != nullptr, "vk" #n);
        DEVICE(LOAD)
#undef LOAD
        check(observed.counter.errors.load() == 0, "Clean native initialization");
    }
    void emit(VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
        VkDebugUtilsMessengerCallbackDataEXT data{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT};
        data.pMessageIdName = "GFX-COUNTER-TEST"; data.messageIdNumber = 0x42; data.pMessage = "Intentional isolated counter control";
        SubmitDebugUtilsMessageEXT(instance, severity, VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT, &data);
    }
    void nativeErrors(bool synchronization = true) {
        observed.abortExpected = true;
        VkBufferCreateInfo empty{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; empty.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        for (unsigned i = 0; i < (synchronization ? 32u : 0u); ++i) {
            VkBuffer buffer{}; const auto result = CreateBuffer(device, &empty, nullptr, &buffer);
            check(result == VK_ERROR_VALIDATION_FAILED_EXT && !buffer, "Validation aborts invalid buffer call before driver");
        }
        const unsigned baseline = synchronization ? 32u : 0u;
        check(observed.sizeErrors == baseline && observed.counter.errors == baseline, "All repeated native VUIDs counted beyond default limit");
        observed.abortExpected = false;
        for (unsigned i = 0; i < 2; ++i) {
            auto desc = empty; desc.size = 4096; desc.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            ok(CreateBuffer(device, &desc, nullptr, &buffers[i]));
            VkMemoryRequirements req{}; GetBufferMemoryRequirements(device, buffers[i], &req);
            unsigned type{}; while (type < memory.memoryTypeCount && !(req.memoryTypeBits & (1u << type))) ++type;
            check(type < memory.memoryTypeCount, "Native memory type");
            VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize = req.size; alloc.memoryTypeIndex = type;
            ok(AllocateMemory(device, &alloc, nullptr, &allocations[i])); ok(BindBufferMemory(device, buffers[i], allocations[i], 0));
        }
        VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; p.queueFamilyIndex = family;
        ok(CreateCommandPool(device, &p, nullptr, &pool));
        VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; a.commandPool = pool; a.commandBufferCount = 1; a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        VkCommandBuffer command{}; ok(AllocateCommandBuffers(device, &a, &command));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; ok(BeginCommandBuffer(command, &begin));
        VkBufferCopy region{0,0,4096}; CmdCopyBuffer(command, buffers[0], buffers[1], 1, &region);
        check(observed.counter.errors == baseline, "First valid native copy is clean");
        observed.abortExpected = true;
        CmdCopyBuffer(command, buffers[0], buffers[1], 1, &region);
        observed.abortExpected = false;
        check(observed.syncErrors == (synchronization ? 1u : 0u) && observed.counter.errors == baseline + (synchronization ? 1u : 0u),
            "Missing-barrier detection follows actual synchronization setting");
        ok(EndCommandBuffer(command)); // Positive-control commands are never submitted.
    }
};
}
int main() try {
    std::cout << "source=" << gfx::buildFingerprint() << " isolated_expected_errors=true" << std::endl;
    unsigned count = 1;
    for (unsigned index = 0; index < count; ++index) {
        Probe primary; primary.initialize(index); count = primary.physicalCount;
        std::cout << "adapter=" << primary.properties.deviceName << std::endl;
        primary.nativeErrors();
        primary.emit(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT); primary.emit(VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT);
        check(primary.observed.counter.errors == 33, "Warnings/information excluded");
        std::vector<std::thread> workers;
        for (unsigned i = 0; i < 4; ++i) workers.emplace_back([&] { for (unsigned j = 0; j < 256; ++j) primary.emit(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT); });
        for (auto& t : workers) t.join();
        check(primary.observed.injected == 1024 && primary.observed.counter.errors == 1057, "Concurrent native callbacks counted without loss");
        Probe independent; independent.initialize(index); independent.emit(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
        check(independent.observed.counter.errors == 1 && primary.observed.counter.errors == 1057, "Instance counters independent");
        independent.close(); primary.close();
        check(!primary.observed.unexpected && !independent.observed.unexpected && primary.observed.counter.errors == 1057 && independent.observed.counter.errors == 1, "Clean destruction and no unexpected errors");
        Probe disabled; disabled.initialize(index, false); disabled.nativeErrors(false); disabled.close();
        check(!disabled.observed.counter.errors && !disabled.observed.unexpected, "Synchronization-disabled negative control stays clean; no commands submitted");
    }
    std::cout << "PASS assertions=" << assertions << " adapters=" << count << " native_core_errors=" << count*32
        << " native_sync_hazards=" << count << " sync_disabled_controls=" << count
        << " synthetic_errors=" << count*1025 << " invalid_GPU_submissions=0" << std::endl;
    return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << std::endl; return 1; }
