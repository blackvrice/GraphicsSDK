#pragma once

#include <graphics/Graphics.hpp>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace pipelineCacheTest {
inline void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template<class T>
T take(gfx::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}

inline void clear(gfx::Device& device) {
    auto status = device.clearGraphicsPipelineCache();
    if (!status) throw std::runtime_error(status.error().message);
}

inline gfx::PipelinePtr exercise(gfx::Device& device, gfx::DeviceDesc selection,
                                 const gfx::GraphicsPipelineDesc& desc, const gfx::ShaderBinary& different) {
    auto cold = take(device.createCachedGraphicsPipeline(desc));
    check(!cold.hit, "First pipeline lookup must miss");
    std::weak_ptr<gfx::Pipeline> retained = cold.pipeline;
    cold.pipeline.reset();
    auto warm = take(device.createCachedGraphicsPipeline(desc));
    check(warm.hit && warm.pipeline == retained.lock(), "Cache retains and returns the same pipeline");
    check(take(device.createGraphicsPipeline(desc)) != warm.pipeline, "Uncached API creates an independent pipeline");

    auto changed = desc;
    check(different != desc.fragment, "Shader change fixture must change the key");
    changed.fragment = different;
    auto shader = take(device.createCachedGraphicsPipeline(changed));
    check(!shader.hit && shader.pipeline != warm.pipeline, "Changed shader must miss");
    check(take(device.createCachedGraphicsPipeline(changed)).hit, "Changed shader is now cached");
    changed = desc;
    changed.renderTargets = gfx::RenderTargetLayout{{gfx::Format::BGRA8UNorm}, {}};
    auto layout = take(device.createCachedGraphicsPipeline(changed));
    check(!layout.hit && layout.pipeline != warm.pipeline, "Changed target layout must miss");
    check(take(device.createCachedGraphicsPipeline(changed)).hit, "Changed layout is now cached");
    changed = desc;
    changed.vertex.stage = gfx::ShaderStage::Fragment;
    const auto bad = device.createCachedGraphicsPipeline(changed);
    check(!bad && bad.error().code == gfx::ErrorCode::InvalidArgument, "Warm bytes must not hide a shader stage error");
    const auto retry = device.createCachedGraphicsPipeline(changed);
    check(!retry && retry.error().code == gfx::ErrorCode::InvalidArgument, "Failed creation stays retryable");
    check(take(device.createCachedGraphicsPipeline(desc)).pipeline == warm.pipeline, "Failed creation preserves cache");

    bool threadRejected = false;
    std::thread wrongThread([&] {
        const auto result = device.createCachedGraphicsPipeline(desc);
        const auto clearResult = device.clearGraphicsPipelineCache();
        threadRejected = !result && result.error().code == gfx::ErrorCode::InvalidState &&
                         !clearResult && clearResult.error().code == gfx::ErrorCode::InvalidState;
    });
    wrongThread.join();
    check(threadRejected, "Lookup and clear enforce device thread affinity");
    {
        auto other = take(gfx::createDevice(selection));
        auto foreign = take(other->createCachedGraphicsPipeline(desc));
        check(!foreign.hit && foreign.pipeline != warm.pipeline, "A new device starts with an empty cache");
    }

    clear(device);
    auto cleared = take(device.createCachedGraphicsPipeline(desc));
    check(!cleared.hit && cleared.pipeline != warm.pipeline, "Explicit clear invalidates lookups");
    auto variant = desc;
    variant.alphaBlend = !desc.alphaBlend;
    std::weak_ptr<gfx::Pipeline> oldest;
    for (unsigned index = 0; index < 8; ++index) {
        variant.depthState.compare = static_cast<gfx::CompareOp>(index);
        auto created = take(device.createCachedGraphicsPipeline(variant));
        check(!created.hit, "Distinct depth or blend key must miss");
        if (index == 0) oldest = created.pipeline;
        if (index == 6) check(take(device.createCachedGraphicsPipeline(desc)).hit, "Recent use protects the original");
    }
    check(oldest.expired(), "Ninth key evicts the least recently used pipeline");
    auto hit = take(device.createCachedGraphicsPipeline(desc));
    check(hit.hit && hit.pipeline == cleared.pipeline, "Recently used pipeline survives eviction");
    clear(device);
    std::cout << "PASS pipeline cache shader/layout/device misses, hit, clear and LRU\n";
    return hit.pipeline; // Still usable after clear.
}
}
