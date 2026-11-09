// Process-local observer for the descriptor baseline's single device.
#include "../common/VulkanObserverDispatch.hpp"
#include "Observer.hpp"
#include <set>

namespace {
using namespace vulkanObserver;
std::set<VkDescriptorPool> livePools;
descriptorBaseline::Counts totals;

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

}

namespace vulkanObserver {
void onDeviceCreated() {
    if (!devices.empty()) ++totals.errors;
}
void onDeviceDestroyed() {
    if (!livePools.empty()) ++totals.errors;
}
PFN_vkVoidFunction objectHook(const char* name) {
#define HOOK(symbol, function) if (!std::strcmp(name, "vk" #symbol)) return reinterpret_cast<PFN_vkVoidFunction>(function)
    HOOK(CreateDescriptorPool, createPool);
    HOOK(DestroyDescriptorPool, destroyPool);
    HOOK(AllocateDescriptorSets, allocateSets);
    HOOK(UpdateDescriptorSets, updateSets);
    HOOK(CmdBindDescriptorSets, bindSets);
#undef HOOK
    return nullptr;
}
}

extern "C" __declspec(dllexport) VKAPI_ATTR VkResult VKAPI_CALL
gfxDescriptorNegotiate(VkNegotiateLayerInterface* value) {
    return vulkanObserver::negotiate(value);
}

extern "C" __declspec(dllexport) int gfxDescriptorSnapshot(descriptorBaseline::Counts* out, unsigned size) {
    if (!out || size != sizeof(*out)) return 0;
    std::lock_guard lock(mutex);
    *out = totals;
    out->live = livePools.size();
    return 1;
}
