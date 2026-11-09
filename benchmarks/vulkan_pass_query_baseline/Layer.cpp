#include "../common/VulkanObserverDispatch.hpp"
#include "Observer.hpp"
#include <algorithm>
#include <set>

namespace {
using namespace vulkanObserver;
using passQuery::Objects;
using passQuery::Counts;

Counts totals;
std::set<VkQueryPool> queries;
std::set<VkRenderPass> passes;
std::map<VkFramebuffer, VkRenderPass> framebuffers;

VKAPI_ATTR VkResult VKAPI_CALL createQuery(VkDevice h, const VkQueryPoolCreateInfo* i, const VkAllocationCallbacks* a,
                                          VkQueryPool* out) {
    const auto result = next<PFN_vkCreateQueryPool>(device(h), "vkCreateQueryPool")(h, i, a, out);
    std::lock_guard lock(mutex);
    ++totals.queries.created;
    if (result != VK_SUCCESS || !queries.insert(*out).second || i->queryType != VK_QUERY_TYPE_TIMESTAMP ||
        i->queryCount != 2) ++totals.errors;
    return result;
}
VKAPI_ATTR void VKAPI_CALL destroyQuery(VkDevice h, VkQueryPool pool, const VkAllocationCallbacks* a) {
    next<PFN_vkDestroyQueryPool>(device(h), "vkDestroyQueryPool")(h, pool, a);
    std::lock_guard lock(mutex);
    ++totals.queries.destroyed;
    if (queries.erase(pool) != 1) ++totals.errors;
}
VKAPI_ATTR VkResult VKAPI_CALL createPass(VkDevice h, const VkRenderPassCreateInfo* i, const VkAllocationCallbacks* a,
                                         VkRenderPass* out) {
    const auto result = next<PFN_vkCreateRenderPass>(device(h), "vkCreateRenderPass")(h, i, a, out);
    std::lock_guard lock(mutex);
    ++totals.passes.created;
    if (result != VK_SUCCESS || !passes.insert(*out).second || i->attachmentCount != 1 || i->subpassCount != 1)
        ++totals.errors;
    return result;
}
VKAPI_ATTR void VKAPI_CALL destroyPass(VkDevice h, VkRenderPass pass, const VkAllocationCallbacks* a) {
    next<PFN_vkDestroyRenderPass>(device(h), "vkDestroyRenderPass")(h, pass, a);
    std::lock_guard lock(mutex);
    ++totals.passes.destroyed;
    if (passes.erase(pass) != 1 ||
        std::ranges::any_of(framebuffers, [&](const auto& item) { return item.second == pass; })) ++totals.errors;
}
VKAPI_ATTR VkResult VKAPI_CALL createFramebuffer(VkDevice h, const VkFramebufferCreateInfo* i,
                                                const VkAllocationCallbacks* a, VkFramebuffer* out) {
    const auto result = next<PFN_vkCreateFramebuffer>(device(h), "vkCreateFramebuffer")(h, i, a, out);
    std::lock_guard lock(mutex);
    ++totals.framebuffers.created;
    if (result != VK_SUCCESS || !framebuffers.emplace(*out, i->renderPass).second ||
        !passes.contains(i->renderPass) || i->attachmentCount != 1) ++totals.errors;
    return result;
}
VKAPI_ATTR void VKAPI_CALL destroyFramebuffer(VkDevice h, VkFramebuffer fb, const VkAllocationCallbacks* a) {
    next<PFN_vkDestroyFramebuffer>(device(h), "vkDestroyFramebuffer")(h, fb, a);
    std::lock_guard lock(mutex);
    ++totals.framebuffers.destroyed;
    if (framebuffers.erase(fb) != 1) ++totals.errors;
}
VKAPI_ATTR void VKAPI_CALL resetQueries(VkCommandBuffer command, VkQueryPool pool, std::uint32_t first, std::uint32_t count) {
    const auto d = device(command);
    next<PFN_vkCmdResetQueryPool>(d, "vkCmdResetQueryPool")(command, pool, first, count);
    std::lock_guard lock(mutex);
    ++totals.resets;
    if (!queries.contains(pool) || first != 0 || count != 2) ++totals.errors;
}
VKAPI_ATTR void VKAPI_CALL writeTimestamp(VkCommandBuffer command, VkPipelineStageFlagBits stage,
                                         VkQueryPool pool, std::uint32_t index) {
    const auto d = device(command);
    next<PFN_vkCmdWriteTimestamp>(d, "vkCmdWriteTimestamp")(command, stage, pool, index);
    std::lock_guard lock(mutex);
    ++totals.writes;
    if (!queries.contains(pool) || index > 1 ||
        stage != (index ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)) ++totals.errors;
}
VKAPI_ATTR VkResult VKAPI_CALL readQueries(VkDevice h, VkQueryPool pool, std::uint32_t first, std::uint32_t count,
                                          std::size_t size, void* data, VkDeviceSize stride, VkQueryResultFlags flags) {
    const auto result = next<PFN_vkGetQueryPoolResults>(device(h), "vkGetQueryPoolResults")(h, pool, first, count,
                                                                                         size, data, stride, flags);
    std::lock_guard lock(mutex);
    ++totals.reads;
    if (result != VK_SUCCESS || !queries.contains(pool) || first != 0 || count != 2) ++totals.errors;
    return result;
}
}

namespace vulkanObserver {
void onDeviceCreated() {
    if (!devices.empty()) ++totals.errors;
}
void onDeviceDestroyed() {
    if (!queries.empty() || !passes.empty() || !framebuffers.empty()) ++totals.errors;
}
PFN_vkVoidFunction objectHook(const char* name) {
#define HOOK(symbol, function) if (!std::strcmp(name, "vk" #symbol)) return reinterpret_cast<PFN_vkVoidFunction>(function)
    HOOK(CreateQueryPool, createQuery);
    HOOK(DestroyQueryPool, destroyQuery);
    HOOK(CreateRenderPass, createPass);
    HOOK(DestroyRenderPass, destroyPass);
    HOOK(CreateFramebuffer, createFramebuffer);
    HOOK(DestroyFramebuffer, destroyFramebuffer);
    HOOK(CmdResetQueryPool, resetQueries);
    HOOK(CmdWriteTimestamp, writeTimestamp);
    HOOK(GetQueryPoolResults, readQueries);
#undef HOOK
    return nullptr;
}
}

extern "C" __declspec(dllexport) VKAPI_ATTR VkResult VKAPI_CALL
gfxPassQueryNegotiate(VkNegotiateLayerInterface* value) {
    return vulkanObserver::negotiate(value);
}

extern "C" __declspec(dllexport) int gfxPassQuerySnapshot(passQuery::Counts* out, unsigned size) {
    if (!out || size != sizeof(*out)) return 0;
    std::lock_guard lock(vulkanObserver::mutex);
    *out = totals;
    out->queries.live = queries.size();
    out->passes.live = passes.size();
    out->framebuffers.live = framebuffers.size();
    return 1;
}
