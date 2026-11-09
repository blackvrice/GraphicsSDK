#pragma once

// Loader dispatch shared by the process-local Vulkan benchmark observers.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <cstring>
#include <map>
#include <mutex>

namespace vulkanObserver {
struct Instance {
    VkInstance handle;
    PFN_vkGetInstanceProcAddr get;
    PFN_GetPhysicalDeviceProcAddr physical;
};
struct Device { VkDevice handle; PFN_vkGetDeviceProcAddr get; };

inline std::map<void*, Instance> instances;
inline std::map<void*, Device> devices;
inline std::mutex mutex;

// Each observer provides these hooks. Device lifecycle hooks run under mutex.
PFN_vkVoidFunction objectHook(const char* name);
void onDeviceCreated();
void onDeviceDestroyed();

inline void* key(const void* handle) { return *static_cast<void* const*>(handle); }
inline Instance instance(const void* handle) {
    std::lock_guard lock(mutex);
    return instances.at(key(handle));
}
inline Device device(const void* handle) {
    std::lock_guard lock(mutex);
    return devices.at(key(handle));
}
template<class Proc> Proc next(Device d, const char* name) {
    return reinterpret_cast<Proc>(d.get(d.handle, name));
}

inline VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* a,
                                                     VkInstance* out) {
    auto link = reinterpret_cast<const VkLayerInstanceCreateInfo*>(info->pNext);
    while (link && (link->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO || link->function != VK_LAYER_LINK_INFO))
        link = reinterpret_cast<const VkLayerInstanceCreateInfo*>(link->pNext);
    if (!link || !link->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    const auto get = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const auto physical = link->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
    const_cast<VkLayerInstanceCreateInfo*>(link)->u.pLayerInfo = link->u.pLayerInfo->pNext;
    const auto result = reinterpret_cast<PFN_vkCreateInstance>(get(nullptr, "vkCreateInstance"))(info, a, out);
    if (result == VK_SUCCESS) {
        std::lock_guard lock(mutex);
        instances.emplace(key(*out), Instance{*out, get, physical});
    }
    return result;
}
inline VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info,
                                                   const VkAllocationCallbacks* a, VkDevice* out) {
    auto link = reinterpret_cast<const VkLayerDeviceCreateInfo*>(info->pNext);
    while (link && (link->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO || link->function != VK_LAYER_LINK_INFO))
        link = reinterpret_cast<const VkLayerDeviceCreateInfo*>(link->pNext);
    if (!link || !link->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;
    const auto create = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const auto get = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    const auto parent = instance(physical);
    const_cast<VkLayerDeviceCreateInfo*>(link)->u.pLayerInfo = link->u.pLayerInfo->pNext;
    const auto result = reinterpret_cast<PFN_vkCreateDevice>(create(parent.handle, "vkCreateDevice"))(physical, info, a, out);
    if (result == VK_SUCCESS) {
        std::lock_guard lock(mutex);
        onDeviceCreated();
        devices.emplace(key(*out), Device{*out, get});
    }
    return result;
}
inline VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance handle, const VkAllocationCallbacks* a) {
    const auto parent = instance(handle);
    const auto dispatchKey = key(handle);
    reinterpret_cast<PFN_vkDestroyInstance>(parent.get(handle, "vkDestroyInstance"))(handle, a);
    std::lock_guard lock(mutex);
    instances.erase(dispatchKey);
}
inline VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice handle, const VkAllocationCallbacks* a) {
    const auto dispatchKey = key(handle);
    next<PFN_vkDestroyDevice>(device(handle), "vkDestroyDevice")(handle, a);
    std::lock_guard lock(mutex);
    onDeviceDestroyed();
    devices.erase(dispatchKey);
}

inline VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice, const char*);
inline PFN_vkVoidFunction deviceHook(const char* name) {
    if (!std::strcmp(name, "vkGetDeviceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(getDevice);
    if (!std::strcmp(name, "vkDestroyDevice")) return reinterpret_cast<PFN_vkVoidFunction>(destroyDevice);
    return objectHook(name);
}
inline VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance handle, const char* name) {
    if (!std::strcmp(name, "vkGetInstanceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(getInstance);
    if (!std::strcmp(name, "vkCreateInstance")) return reinterpret_cast<PFN_vkVoidFunction>(createInstance);
    if (!std::strcmp(name, "vkDestroyInstance")) return reinterpret_cast<PFN_vkVoidFunction>(destroyInstance);
    if (!std::strcmp(name, "vkCreateDevice")) return reinterpret_cast<PFN_vkVoidFunction>(createDevice);
    if (auto hook = deviceHook(name)) return hook;
    return handle ? instance(handle).get(handle, name) : nullptr;
}
inline VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice handle, const char* name) {
    if (auto hook = deviceHook(name)) return hook;
    return handle ? device(handle).get(handle, name) : nullptr;
}
inline VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getPhysical(VkInstance handle, const char* name) {
    if (!handle) return nullptr;
    const auto parent = instance(handle);
    return parent.physical ? parent.physical(handle, name) : parent.get(handle, name);
}
inline VkResult negotiate(VkNegotiateLayerInterface* value) {
    if (!value || value->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT || value->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;
    value->loaderLayerInterfaceVersion = 2;
    value->pfnGetInstanceProcAddr = getInstance;
    value->pfnGetDeviceProcAddr = getDevice;
    value->pfnGetPhysicalDeviceProcAddr = getPhysical;
    return VK_SUCCESS;
}
}
