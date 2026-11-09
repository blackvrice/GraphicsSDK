#include "../common/VulkanObserverDispatch.hpp"
#include "Observer.hpp"
#include <set>

namespace {
using namespace vulkanObserver;
submitBaseline::Counts totals;
std::set<VkCommandPool> pools;
std::map<VkFence, bool> fences;
VkFence lastSubmit{};

VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice h, const VkCommandPoolCreateInfo* i, const VkAllocationCallbacks* a,
                                         VkCommandPool* out) {
    const auto result = next<PFN_vkCreateCommandPool>(device(h), "vkCreateCommandPool")(h, i, a, out);
    std::lock_guard lock(mutex);
    ++totals.pools;
    if (result != VK_SUCCESS || !pools.insert(*out).second) ++totals.errors;
    return result;
}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice h, VkCommandPool pool, const VkAllocationCallbacks* a) {
    next<PFN_vkDestroyCommandPool>(device(h), "vkDestroyCommandPool")(h, pool, a);
    std::lock_guard lock(mutex);
    ++totals.destroyedPools;
    if (pools.erase(pool) != 1) ++totals.errors;
}
VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice h, const VkCommandBufferAllocateInfo* i, VkCommandBuffer* out) {
    const auto result = next<PFN_vkAllocateCommandBuffers>(device(h), "vkAllocateCommandBuffers")(h, i, out);
    std::lock_guard lock(mutex);
    totals.commandBuffers += i->commandBufferCount;
    if (result != VK_SUCCESS || i->commandBufferCount != 1 || !pools.contains(i->commandPool)) ++totals.errors;
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL createFence(VkDevice h, const VkFenceCreateInfo* i, const VkAllocationCallbacks* a,
                                          VkFence* out) {
    const auto result = next<PFN_vkCreateFence>(device(h), "vkCreateFence")(h, i, a, out);
    std::lock_guard lock(mutex);
    ++totals.fences;
    if (result != VK_SUCCESS || !fences.emplace(*out, false).second || i->flags != 0) ++totals.errors;
    return result;
}
VKAPI_ATTR void VKAPI_CALL destroyFence(VkDevice h, VkFence fence, const VkAllocationCallbacks* a) {
    next<PFN_vkDestroyFence>(device(h), "vkDestroyFence")(h, fence, a);
    std::lock_guard lock(mutex);
    ++totals.destroyedFences;
    const auto found = fences.find(fence);
    if (found == fences.end()) {
        ++totals.errors;
        return;
    }
    if (!found->second) ++totals.errors;
    fences.erase(found);
}
VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue queue, std::uint32_t count, const VkSubmitInfo* info, VkFence fence) {
    const auto d = device(queue);
    const auto result = next<PFN_vkQueueSubmit>(d, "vkQueueSubmit")(queue, count, info, fence);
    std::lock_guard lock(mutex);
    ++totals.submissions;
    lastSubmit = fence;
    if (result != VK_SUCCESS || !fences.contains(fence) || count != 1 || info[0].commandBufferCount != 1 ||
        info[0].waitSemaphoreCount != 0 || info[0].signalSemaphoreCount != 0) ++totals.errors;
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL status(VkDevice h, VkFence fence) {
    const auto result = next<PFN_vkGetFenceStatus>(device(h), "vkGetFenceStatus")(h, fence);
    std::lock_guard lock(mutex);
    const auto found = fences.find(fence);
    if (found == fences.end() || found->second) ++totals.errors;
    else if (result == VK_SUCCESS) {
        ++totals.ready;
        found->second = true;
    } else if (result == VK_NOT_READY) {
        ++totals.notReady;
    } else {
        ++totals.errors;
    }
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL wait(VkDevice h, std::uint32_t count, const VkFence* list, VkBool32 all,
                                   std::uint64_t timeout) {
    const auto result = next<PFN_vkWaitForFences>(device(h), "vkWaitForFences")(h, count, list, all, timeout);
    std::lock_guard lock(mutex);
    ++totals.waits;
    if (result != VK_SUCCESS || count != 1 || !all || !timeout || timeout == UINT64_MAX ||
        list[0] != lastSubmit || !fences.contains(list[0])) ++totals.errors;
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL queueIdle(VkQueue q) {
    const auto d = device(q);
    const auto result = next<PFN_vkQueueWaitIdle>(d, "vkQueueWaitIdle")(q);
    std::lock_guard lock(mutex);
    ++totals.idle;
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL deviceIdle(VkDevice h) {
    const auto result = next<PFN_vkDeviceWaitIdle>(device(h), "vkDeviceWaitIdle")(h);
    std::lock_guard lock(mutex);
    ++totals.idle;
    return result;
}
VKAPI_ATTR void VKAPI_CALL copy(VkCommandBuffer cmd, VkBuffer from, VkBuffer to, std::uint32_t count,
                               const VkBufferCopy* regions) {
    const auto d = device(cmd);
    next<PFN_vkCmdCopyBuffer>(d, "vkCmdCopyBuffer")(cmd, from, to, count, regions);
    std::lock_guard lock(mutex);
    ++totals.copies;
    if (count != 1 || regions[0].srcOffset || regions[0].dstOffset || regions[0].size != 4096)
        ++totals.errors;
}
}

namespace vulkanObserver {
void onDeviceCreated() {
    if (!devices.empty()) ++totals.errors;
}
void onDeviceDestroyed() {
    if (!pools.empty() || !fences.empty()) ++totals.errors;
}
PFN_vkVoidFunction objectHook(const char* name) {
#define HOOK(symbol, function) if (!std::strcmp(name, "vk" #symbol)) return reinterpret_cast<PFN_vkVoidFunction>(function)
    HOOK(CreateCommandPool, createPool);
    HOOK(DestroyCommandPool, destroyPool);
    HOOK(AllocateCommandBuffers, allocate);
    HOOK(CreateFence, createFence);
    HOOK(DestroyFence, destroyFence);
    HOOK(QueueSubmit, submit);
    HOOK(GetFenceStatus, status);
    HOOK(WaitForFences, wait);
    HOOK(QueueWaitIdle, queueIdle);
    HOOK(DeviceWaitIdle, deviceIdle);
    HOOK(CmdCopyBuffer, copy);
#undef HOOK
    return nullptr;
}
}

extern "C" __declspec(dllexport) VKAPI_ATTR VkResult VKAPI_CALL
gfxSubmitNegotiate(VkNegotiateLayerInterface* value) {
    return vulkanObserver::negotiate(value);
}

extern "C" __declspec(dllexport) int gfxSubmitSnapshot(submitBaseline::Counts* out, unsigned size) {
    if (!out || size != sizeof(*out)) return 0;
    std::lock_guard lock(vulkanObserver::mutex);
    *out = totals;
    out->livePools = pools.size();
    out->liveFences = fences.size();
    return 1;
}
