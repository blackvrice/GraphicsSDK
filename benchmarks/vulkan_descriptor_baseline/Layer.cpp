// Process-local observer for the descriptor baseline's single device.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "Observer.hpp"
#include <cstring>
#include <map>
#include <mutex>
#include <set>

namespace {
struct Instance {
    VkInstance handle;
    PFN_vkGetInstanceProcAddr get;
    PFN_GetPhysicalDeviceProcAddr physical;
};
struct Device { VkDevice handle; PFN_vkGetDeviceProcAddr get; };

std::map<void*, Instance> instances;
std::map<void*, Device> devices;
std::set<VkDescriptorPool> livePools;
std::mutex mutex;
descriptorBaseline::Counts totals;

void* key(const void* handle) { return *static_cast<void* const*>(handle); }
Instance instance(const void* handle) {
    std::lock_guard lock(mutex);
    return instances.at(key(handle));
}
Device device(const void* handle) {
    std::lock_guard lock(mutex);
    return devices.at(key(handle));
}
template<class Proc> Proc next(Device d, const char* name) {
    return reinterpret_cast<Proc>(d.get(d.handle, name));
}

VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* a, VkInstance* out) {
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
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info,
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
        if (!devices.empty()) ++totals.errors;
        devices.emplace(key(*out), Device{*out, get});
    }
    return result;
}
VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance handle, const VkAllocationCallbacks* a) {
    const auto parent = instance(handle);
    const auto dispatchKey = key(handle);
    reinterpret_cast<PFN_vkDestroyInstance>(parent.get(handle, "vkDestroyInstance"))(handle, a);
    std::lock_guard lock(mutex);
    instances.erase(dispatchKey);
}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice handle, const VkAllocationCallbacks* a) {
    const auto dispatchKey = key(handle);
    next<PFN_vkDestroyDevice>(device(handle), "vkDestroyDevice")(handle, a);
    std::lock_guard lock(mutex);
    if (!livePools.empty()) ++totals.errors;
    devices.erase(dispatchKey);
}

VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice handle, const VkDescriptorPoolCreateInfo* info,
                                         const VkAllocationCallbacks* a, VkDescriptorPool* out) {
    const auto result = next<PFN_vkCreateDescriptorPool>(device(handle), "vkCreateDescriptorPool")(handle, info, a, out);
    std::lock_guard lock(mutex);
    ++totals.create;
    if (result != VK_SUCCESS || !livePools.insert(*out).second || info->maxSets != 1) ++totals.errors;
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL allocateSets(VkDevice handle, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* out) {
    const auto result = next<PFN_vkAllocateDescriptorSets>(device(handle), "vkAllocateDescriptorSets")(handle, info, out);
    std::lock_guard lock(mutex);
    ++totals.allocate;
    if (result != VK_SUCCESS || info->descriptorSetCount != 1 || !livePools.contains(info->descriptorPool)) ++totals.errors;
    return result;
}
VKAPI_ATTR void VKAPI_CALL updateSets(VkDevice handle, std::uint32_t count, const VkWriteDescriptorSet* writes,
                                     std::uint32_t copies, const VkCopyDescriptorSet* copy) {
    next<PFN_vkUpdateDescriptorSets>(device(handle), "vkUpdateDescriptorSets")(handle, count, writes, copies, copy);
    std::lock_guard lock(mutex);
    ++totals.update;
    if (count != 1 || copies || writes[0].descriptorCount != 1 ||
        writes[0].descriptorType != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) ++totals.errors;
}
VKAPI_ATTR void VKAPI_CALL bindSets(VkCommandBuffer command, VkPipelineBindPoint point, VkPipelineLayout layout,
                                   std::uint32_t first, std::uint32_t count, const VkDescriptorSet* sets,
                                   std::uint32_t offsets, const std::uint32_t* dynamicOffsets) {
    const auto d = device(command);
    next<PFN_vkCmdBindDescriptorSets>(d, "vkCmdBindDescriptorSets")(command, point, layout, first, count, sets,
                                                                   offsets, dynamicOffsets);
    std::lock_guard lock(mutex);
    ++totals.bind;
    if (point != VK_PIPELINE_BIND_POINT_GRAPHICS || first || count != 1 || offsets) ++totals.errors;
}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice handle, VkDescriptorPool pool, const VkAllocationCallbacks* a) {
    next<PFN_vkDestroyDescriptorPool>(device(handle), "vkDestroyDescriptorPool")(handle, pool, a);
    std::lock_guard lock(mutex);
    ++totals.destroy;
    if (livePools.erase(pool) != 1) ++totals.errors;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance, const char*);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice, const char*);
PFN_vkVoidFunction deviceHook(const char* name) {
#define HOOK(symbol, function) if (!std::strcmp(name, "vk" #symbol)) return reinterpret_cast<PFN_vkVoidFunction>(function)
    HOOK(GetDeviceProcAddr, getDevice);
    HOOK(DestroyDevice, destroyDevice);
    HOOK(CreateDescriptorPool, createPool);
    HOOK(DestroyDescriptorPool, destroyPool);
    HOOK(AllocateDescriptorSets, allocateSets);
    HOOK(UpdateDescriptorSets, updateSets);
    HOOK(CmdBindDescriptorSets, bindSets);
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance handle, const char* name) {
    HOOK(GetInstanceProcAddr, getInstance);
    HOOK(CreateInstance, createInstance);
    HOOK(DestroyInstance, destroyInstance);
    HOOK(CreateDevice, createDevice);
#undef HOOK
    if (auto hook = deviceHook(name)) return hook;
    return handle ? instance(handle).get(handle, name) : nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice handle, const char* name) {
    if (auto hook = deviceHook(name)) return hook;
    return handle ? device(handle).get(handle, name) : nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getPhysical(VkInstance handle, const char* name) {
    if (!handle) return nullptr;
    const auto parent = instance(handle);
    return parent.physical ? parent.physical(handle, name) : parent.get(handle, name);
}
}

extern "C" __declspec(dllexport) VKAPI_ATTR VkResult VKAPI_CALL
gfxDescriptorNegotiate(VkNegotiateLayerInterface* value) {
    if (!value || value->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT || value->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;
    value->loaderLayerInterfaceVersion = 2;
    value->pfnGetInstanceProcAddr = getInstance;
    value->pfnGetDeviceProcAddr = getDevice;
    value->pfnGetPhysicalDeviceProcAddr = getPhysical;
    return VK_SUCCESS;
}

extern "C" __declspec(dllexport) int gfxDescriptorSnapshot(descriptorBaseline::Counts* out, unsigned size) {
    if (!out || size != sizeof(*out)) return 0;
    std::lock_guard lock(mutex);
    *out = totals;
    out->live = livePools.size();
    return 1;
}
