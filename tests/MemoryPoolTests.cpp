#include <graphics/Graphics.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Totals {
    std::uint64_t assertions{};
    std::uint64_t rejections{};
    std::uint64_t frames{};
};

void check(bool condition, const char* message, Totals& totals) {
    if (!condition) throw std::runtime_error(message);
    ++totals.assertions;
}

template <class T>
T take(gfx::Result<T>&& value, const char* message, Totals& totals) {
    if (!value) {
        throw std::runtime_error(std::string(message) + ": " + value.error().message +
            " (code=" + std::to_string(static_cast<int>(value.error().code)) +
            ", native=" + std::to_string(value.error().nativeCode) + ")");
    }
    ++totals.assertions;
    return std::move(*value);
}

void take(gfx::Result<void>&& value, const char* message, Totals& totals) {
    if (!value) {
        throw std::runtime_error(std::string(message) + ": " + value.error().message +
            " (code=" + std::to_string(static_cast<int>(value.error().code)) +
            ", native=" + std::to_string(value.error().nativeCode) + ")");
    }
    ++totals.assertions;
}

template <class T>
void reject(const gfx::Result<T>& value, gfx::ErrorCode expected, const char* message, Totals& totals) {
    if (value) throw std::runtime_error(std::string("Unexpected success: ") + message);
    if (value.error().code != expected) {
        throw std::runtime_error(std::string(message) + " (expected code=" +
            std::to_string(static_cast<int>(expected)) + ", got code=" +
            std::to_string(static_cast<int>(value.error().code)) + ": " + value.error().message + ")");
    }
    ++totals.assertions;
    ++totals.rejections;
}

void testCpuDescriptorValidation(Totals& totals) {
    const auto badQueue = static_cast<gfx::QueueType>(77);
    take(gfx::validateUploadRingDesc({}), "default UploadRingDesc valid", totals);
    reject(gfx::validateUploadRingDesc({4096, 256, 1, gfx::QueueType::Graphics}),
           gfx::ErrorCode::InvalidArgument, "segmentCount=1 rejected", totals);
    reject(gfx::validateUploadRingDesc({4096, 256, 17, gfx::QueueType::Graphics}),
           gfx::ErrorCode::InvalidArgument, "segmentCount=17 rejected", totals);
    reject(gfx::validateUploadRingDesc({4096, 3, 2, gfx::QueueType::Graphics}),
           gfx::ErrorCode::InvalidArgument, "alignment=3 rejected", totals);
    reject(gfx::validateUploadRingDesc({4096, 200, 2, gfx::QueueType::Graphics}),
           gfx::ErrorCode::InvalidArgument, "non-pow2 alignment rejected", totals);
    reject(gfx::validateUploadRingDesc({4096, 131072, 2, gfx::QueueType::Graphics}),
           gfx::ErrorCode::InvalidArgument, "alignment>65536 rejected", totals);
    reject(gfx::validateUploadRingDesc({256, 256, 2, gfx::QueueType::Graphics}),
           gfx::ErrorCode::InvalidArgument, "capacity < alignment*segmentCount rejected", totals);
    reject(gfx::validateUploadRingDesc({4096, 256, 2, badQueue}),
           gfx::ErrorCode::InvalidArgument, "bad queue UploadRingDesc rejected", totals);

    take(gfx::validateReadbackPoolDesc({}), "default ReadbackPoolDesc valid", totals);
    reject(gfx::validateReadbackPoolDesc({0, 8, gfx::QueueType::Graphics}),
           gfx::ErrorCode::InvalidArgument, "ReadbackPool maxPooledBytes=0 rejected", totals);
    reject(gfx::validateReadbackPoolDesc({4096, 0, gfx::QueueType::Graphics}),
           gfx::ErrorCode::InvalidArgument, "ReadbackPool maxPooledBuffers=0 rejected", totals);
    reject(gfx::validateReadbackPoolDesc({4096, 8, badQueue}),
           gfx::ErrorCode::InvalidArgument, "ReadbackPool bad queue rejected", totals);

    take(gfx::validateTransientPoolDesc({}), "default TransientPoolDesc valid", totals);
    reject(gfx::validateTransientPoolDesc({0, 8, gfx::QueueType::Graphics, {}}),
           gfx::ErrorCode::InvalidArgument, "TransientPool maxPooledBytes=0 rejected", totals);
    reject(gfx::validateTransientPoolDesc({4096, 0, gfx::QueueType::Graphics, {}}),
           gfx::ErrorCode::InvalidArgument, "TransientPool maxPooledResources=0 rejected", totals);
    reject(gfx::validateTransientPoolDesc({4096, 8, badQueue, {}}),
           gfx::ErrorCode::InvalidArgument, "TransientPool bad queue rejected", totals);
    reject(gfx::validateTransientPoolDesc({4096, 8, gfx::QueueType::Graphics, {true, 0.0, 0.9, 0}}),
           gfx::ErrorCode::InvalidArgument, "BudgetPolicy soft<=0 rejected", totals);
    reject(gfx::validateTransientPoolDesc({4096, 8, gfx::QueueType::Graphics, {true, 0.9, 0.8, 0}}),
           gfx::ErrorCode::InvalidArgument, "BudgetPolicy soft>=hard rejected", totals);
    reject(gfx::validateTransientPoolDesc({4096, 8, gfx::QueueType::Graphics, {true, 0.8, 1.05, 0}}),
           gfx::ErrorCode::InvalidArgument, "BudgetPolicy hard>1 rejected", totals);
    reject(gfx::validateTransientPoolDesc({4096, 8, gfx::QueueType::Graphics, {true, std::numeric_limits<double>::quiet_NaN(), 0.9, 0}}),
           gfx::ErrorCode::InvalidArgument, "BudgetPolicy NaN rejected", totals);
}

void testUploadRingLifecycleAndGpuVerification(gfx::Device& device, Totals& totals) {
    gfx::UploadRingDesc desc{};
    desc.capacity = 3u * 4096u; // 3 segments x 4096 bytes
    desc.alignment = 256u;
    desc.segmentCount = 3u;
    desc.queue = gfx::QueueType::Copy;

    auto ring = take(device.createUploadRing(desc), "createUploadRing(Copy)", totals);
    auto* copyQ = take(device.queue(gfx::QueueType::Copy), "queue(Copy)", totals);
    auto stats0 = take(ring->statistics(), "ring initial stats", totals);
    check(stats0.capacityBytes == 12288u && stats0.segmentBytes == 4096u && stats0.availableBytes == 12288u,
          "initial ring capacity and availability", totals);

    // 1. Sub-allocation alignment & discardPending().
    auto a0 = take(ring->allocate(100), "alloc 100B", totals);
    check(a0.offset == 0 && a0.size == 100 && a0.segmentIndex == 0, "first alloc at offset 0", totals);
    auto a1 = take(ring->allocate(300), "alloc 300B", totals);
    check(a1.offset == 256 && a1.size == 300 && a1.segmentIndex == 0, "second alloc aligned to 256", totals);
    auto a2 = take(ring->allocate(512, 512), "alloc 512B align 512", totals);
    check(a2.offset == 1024 && a2.size == 512 && a2.segmentIndex == 0, "third alloc aligned to 512", totals);
    reject(ring->allocate(0), gfx::ErrorCode::InvalidArgument, "alloc 0 rejected", totals);
    reject(ring->allocate(4097), gfx::ErrorCode::InvalidArgument, "alloc > segmentBytes rejected", totals);
    reject(ring->allocate(64, 300), gfx::ErrorCode::InvalidArgument, "non-pow2 override alignment rejected", totals);

    take(ring->discardPending(), "discardPending", totals);
    auto statsAfterDiscard = take(ring->statistics(), "stats after discard", totals);
    check(statsAfterDiscard.activeBytes == 0 && statsAfterDiscard.availableBytes == 12288u,
          "discardPending restores full capacity", totals);

    // 2. Deterministic OOM saturation across all 3 segments + recovery via discardPending.
    (void)take(ring->allocate(4096), "fill seg 0", totals);
    (void)take(ring->allocate(4096), "fill seg 1", totals);
    (void)take(ring->allocate(4096), "fill seg 2", totals);
    reject(ring->allocate(256), gfx::ErrorCode::OutOfMemory, "4th segment allocation saturates ring (OOM)", totals);
    take(ring->discardPending(), "discard saturated segments", totals);

    // 3. Multi-frame GPU copyBufferRegion wrap verification (10 frames across 3 segments -> >= 3 wraps!).
    constexpr std::uint32_t kFrames = 10;
    constexpr std::uint64_t kChunkBytes = 1024;
    auto devBuf = take(device.createBuffer({kChunkBytes * 2u, gfx::Memory::Device}), "ring devBuf", totals);
    auto rbBuf = take(device.createBuffer({kChunkBytes * 2u, gfx::Memory::Readback}), "ring rbBuf", totals);
    auto copyPool = take(device.createCommandPool(gfx::QueueType::Copy), "ring copyPool", totals);
    auto copyCmd = take(copyPool->createCommandList(), "ring copyCmd", totals);

    for (std::uint32_t f = 0; f < kFrames; ++f) {
        if (f > 0) take(copyPool->reset(), "reset ring copyPool", totals);
        std::vector<std::uint32_t> partA(kChunkBytes / 4u), partB(kChunkBytes / 4u);
        for (std::uint32_t i = 0; i < partA.size(); ++i) {
            partA[i] = 0x10000000u + f * 10000u + i;
            partB[i] = 0x20000000u + f * 10000u + i;
        }
        auto upA = take(ring->upload(std::as_bytes(std::span{partA}), 256), "upload partA", totals);
        auto upB = take(ring->upload(std::as_bytes(std::span{partB}), 256), "upload partB", totals);
        check(upA.buffer == upB.buffer && upB.offset == upA.offset + kChunkBytes,
              "two uploads pack contiguously in one segment", totals);

        take(copyCmd->begin(), "ring copy begin", totals);
        take(copyCmd->transition(devBuf, gfx::ResourceState::CopyDestination), "devBuf -> CopyDst", totals);
        take(copyCmd->copyBufferRegion(upA.buffer, upA.offset, devBuf, 0, kChunkBytes), "copy partA", totals);
        take(copyCmd->copyBufferRegion(upB.buffer, upB.offset, devBuf, kChunkBytes, kChunkBytes), "copy partB", totals);
        take(copyCmd->transition(devBuf, gfx::ResourceState::CopySource), "devBuf -> CopySrc", totals);
        take(copyCmd->copyBuffer(devBuf, rbBuf, kChunkBytes * 2u), "copy devBuf -> rbBuf", totals);
        take(copyCmd->end(), "ring copy end", totals);

        auto fence = take(copyQ->submit(*copyCmd), "submit ring copy", totals);
        reject(ring->commit({0, fence.device}), gfx::ErrorCode::InvalidArgument, "commit fence value=0 rejected", totals);
        reject(ring->commit({fence.value, 0}), gfx::ErrorCode::InvalidArgument, "commit fence device=0 rejected", totals);
        take(ring->commit(fence), "commit ring fence", totals);
        if (f > 0) {
            reject(ring->commit({fence.value - 1u, fence.device}), gfx::ErrorCode::InvalidArgument,
                   "non-monotonic commit rejected", totals);
        }
        reject(ring->commit({fence.value + 1u, fence.device + 9999u}), gfx::ErrorCode::WrongDevice,
               "wrong-device commit fence rejected", totals);

        take(copyQ->wait(fence, std::chrono::seconds(10)), "wait ring copy", totals);
        (void)take(ring->reclaim(), "reclaim ring", totals);

        auto out = take(rbBuf->read(0, kChunkBytes * 2u), "readback ring copy", totals);
        if (std::memcmp(out.data(), partA.data(), kChunkBytes) != 0 ||
            std::memcmp(out.data() + kChunkBytes, partB.data(), kChunkBytes) != 0) {
            throw std::runtime_error("UploadRing readback mismatch at frame " + std::to_string(f));
        }
        ++totals.assertions;
        ++totals.frames;
    }

    auto finalStats = take(ring->statistics(), "ring final stats", totals);
    check(finalStats.totalWrapCount >= 2u, "ring must have wrapped at least twice", totals);
    check(finalStats.totalReclaims >= kFrames, "ring must have reclaimed all submitted frames", totals);
    check(finalStats.inFlightSegments == 0 && finalStats.availableBytes == finalStats.capacityBytes,
          "ring must be completely free after drain", totals);

    // 4. UploadRing + copyBufferToTextureRegion (512B aligned texture upload!).
    auto tex = take(device.createTexture({16, 16, gfx::Format::RGBA8UNorm}), "ring tex", totals);
    const auto fp = tex->footprint();
    std::vector<std::byte> texPixels(fp.size, std::byte{0});
    for (std::uint32_t y = 0; y < 16; ++y) {
        for (std::uint32_t x = 0; x < 16; ++x) {
            const std::size_t off = static_cast<std::size_t>(y) * fp.rowPitch + x * 4u;
            texPixels[off + 0] = static_cast<std::byte>(x * 16u);
            texPixels[off + 1] = static_cast<std::byte>(y * 16u);
            texPixels[off + 2] = static_cast<std::byte>(0xA5u);
            texPixels[off + 3] = static_cast<std::byte>(0xFFu);
        }
    }
    auto texAlloc = take(ring->upload(texPixels, 512u), "ring upload texture (512B aligned)", totals);
    check((texAlloc.offset % 512u) == 0, "texture upload offset 512B aligned", totals);
    auto texRb = take(device.createBuffer({fp.size, gfx::Memory::Readback}), "ring texRb", totals);
    take(copyPool->reset(), "reset copyPool for tex", totals);
    take(copyCmd->begin(), "tex copy begin", totals);
    take(copyCmd->transition(tex, gfx::ResourceState::CopyDestination), "tex -> CopyDst", totals);
    take(copyCmd->copyBufferToTextureRegion(texAlloc.buffer, tex, {texAlloc.offset, fp.rowPitch, 0, 0, 16, 16}),
         "copyBufferToTextureRegion from ring", totals);
    take(copyCmd->transition(tex, gfx::ResourceState::CopySource), "tex -> CopySrc", totals);
    take(copyCmd->copyTextureToBuffer(tex, texRb), "copyTextureToBuffer", totals);
    take(copyCmd->end(), "tex copy end", totals);
    auto texFence = take(copyQ->submit(*copyCmd), "submit tex ring copy", totals);
    take(ring->commit(texFence), "commit tex ring fence", totals);
    take(copyQ->wait(texFence, std::chrono::seconds(10)), "wait tex ring copy", totals);
    auto gotPixels = take(texRb->read(0, fp.size), "readback ring tex", totals);
    for (std::uint32_t y = 0; y < 16; ++y) {
        const std::size_t rowOff = static_cast<std::size_t>(y) * fp.rowPitch;
        if (std::memcmp(gotPixels.data() + rowOff, texPixels.data() + rowOff, 16u * 4u) != 0) {
            throw std::runtime_error("UploadRing texture pixel mismatch at row " + std::to_string(y));
        }
    }
    ++totals.assertions;
}

void testReadbackPoolLifecycle(gfx::Device& device, Totals& totals) {
    gfx::ReadbackPoolDesc desc{};
    desc.maxPooledBytes = 8192u;
    desc.maxPooledBuffers = 2u;
    desc.queue = gfx::QueueType::Graphics;

    auto pool = take(device.createReadbackPool(desc), "createReadbackPool", totals);
    reject(pool->acquire(0), gfx::ErrorCode::InvalidArgument, "ReadbackPool acquire(0) rejected", totals);
    reject(pool->recycle(nullptr), gfx::ErrorCode::InvalidArgument, "ReadbackPool recycle(null) rejected", totals);

    // Non-pool buffer rejected as WrongDevice.
    auto externalRb = take(device.createBuffer({1024, gfx::Memory::Readback}), "external rb", totals);
    reject(pool->recycle(externalRb), gfx::ErrorCode::WrongDevice, "recycle external Readback buffer rejected", totals);
    auto externalUp = take(device.createBuffer({1024, gfx::Memory::Upload}), "external up", totals);
    reject(pool->recycle(externalUp), gfx::ErrorCode::InvalidArgument, "recycle Upload buffer rejected", totals);

    auto rb1 = take(pool->acquire(1000), "rb acquire 1000B", totals);
    const gfx::Buffer* rawPtr1 = rb1.get();
    check(rb1->description().size == 1024u, "1000B rounded to 1024B power-of-two bucket", totals);

    // Copy real GPU data into rb1, recycle with Fence, verify fence-gated retirement and reuse hit!
    auto up = take(device.createBuffer({1024, gfx::Memory::Upload}), "rb test upload", totals);
    std::array<std::uint32_t, 256> pattern{};
    for (std::uint32_t i = 0; i < 256; ++i) pattern[i] = 0x55AA0000u | i;
    take(up->write(0, std::as_bytes(std::span{pattern})), "write pattern", totals);
    auto cmd = take(device.createCommandList(), "rb test cmd", totals);
    take(cmd->begin(), "rb cmd begin", totals);
    take(cmd->copyBuffer(up, rb1, 1024), "copy up -> rb1", totals);
    take(cmd->end(), "rb cmd end", totals);
    auto fence = take(device.queue().submit(*cmd), "submit rb1 copy", totals);
    take(device.queue().wait(fence, std::chrono::seconds(10)), "wait rb1 copy", totals);
    auto got = take(rb1->read(0, 1024), "read rb1", totals);
    check(std::memcmp(got.data(), pattern.data(), 1024) == 0, "rb1 exact bytes", totals);

    reject(pool->recycle(rb1, {1, 0}), gfx::ErrorCode::InvalidArgument, "recycle invalid fence device=0 rejected", totals);
    take(pool->recycle(rb1, fence), "recycle rb1", totals);
    reject(pool->recycle(rb1), gfx::ErrorCode::InvalidState, "double recycle rb1 rejected", totals);
    rb1.reset();

    auto rbReused = take(pool->acquire(900), "rb acquire 900B (hit)", totals);
    check(rbReused.get() == rawPtr1, "ReadbackPool must reuse recycled 1024B buffer for 900B request", totals);
    auto stats = take(pool->statistics(), "rb stats after hit", totals);
    check(stats.hits == 1 && stats.misses == 1, "ReadbackPool 1 hit, 1 miss", totals);

    // Cap eviction test (maxPooledBuffers = 2): acquire 3 buffers and recycle all 3.
    auto rbB = take(pool->acquire(1024), "rb acquire B", totals);
    auto rbC = take(pool->acquire(1024), "rb acquire C", totals);
    take(pool->recycle(std::move(rbReused)), "recycle reused", totals);
    take(pool->recycle(std::move(rbB)), "recycle B", totals);
    take(pool->recycle(std::move(rbC)), "recycle C (triggers eviction)", totals);
    stats = take(pool->statistics(), "rb stats after cap eviction", totals);
    check(stats.pooledCount == 2u && stats.evictions >= 1u, "ReadbackPool enforces maxPooledBuffers=2", totals);

    const auto trimmed = take(pool->trim(0), "rb trim(0)", totals);
    check(trimmed == 2048u, "rb trim(0) frees 2048B", totals);
    stats = take(pool->statistics(), "rb stats after trim", totals);
    check(stats.pooledCount == 0 && stats.pooledBytes == 0, "ReadbackPool empty after trim(0)", totals);
}

void testTransientPoolAndBudgetPressure(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    gfx::TransientPoolDesc desc{};
    desc.maxPooledBytes = 16u * 1024u * 1024u;
    desc.maxPooledResources = 16u;
    desc.queue = gfx::QueueType::Graphics;
    desc.budgetPolicy.enabled = true;
    desc.budgetPolicy.softPressureRatio = 0.80;
    desc.budgetPolicy.hardPressureRatio = 0.92;
    desc.budgetPolicy.softTargetBytes = 512u * 1024u; // 512 KiB soft target

    auto pool = take(device.createTransientResourcePool(desc), "createTransientResourcePool", totals);

    // 1. Buffer + Texture acquire -> render -> recycle -> re-acquire HIT -> render new color!
    gfx::BufferDesc bDesc{4096, gfx::Memory::Device, true};
    gfx::TextureDesc tDesc{64, 64, gfx::Format::RGBA8UNorm, true};

    auto buf1 = take(pool->acquireBuffer(bDesc), "transient acquireBuffer 1", totals);
    auto tex1 = take(pool->acquireTexture(tDesc), "transient acquireTexture 1", totals);
    const gfx::Buffer* rawBuf1 = buf1.get();
    const gfx::Texture* rawTex1 = tex1.get();
    const auto fp = tex1->footprint();
    auto rb = take(device.createBuffer({fp.size, gfx::Memory::Readback}), "transient tex rb", totals);

    auto cmd = take(device.createCommandList(), "transient cmd", totals);
    take(cmd->begin(), "transient begin 1", totals);
    take(cmd->transition(tex1, gfx::ResourceState::RenderTarget), "tex1 -> RT", totals);
    take(cmd->beginRenderPass(tex1, {0.25f, 0.50f, 0.75f, 1.0f}), "clear tex1 color1", totals);
    take(cmd->endRenderPass(), "end pass 1", totals);
    take(cmd->transition(tex1, gfx::ResourceState::CopySource), "tex1 -> CopySrc", totals);
    take(cmd->copyTextureToBuffer(tex1, rb), "copy tex1 -> rb", totals);
    take(cmd->end(), "transient end 1", totals);
    auto f1 = take(device.queue().submit(*cmd), "submit transient 1", totals);
    take(device.queue().wait(f1, std::chrono::seconds(10)), "wait transient 1", totals);

    take(pool->recycle(buf1, f1), "recycle buf1", totals);
    take(pool->recycle(tex1, f1), "recycle tex1", totals);
    reject(pool->recycle(buf1), gfx::ErrorCode::InvalidState, "double recycle transient buf rejected", totals);
    reject(pool->recycle(tex1), gfx::ErrorCode::InvalidState, "double recycle transient tex rejected", totals);
    buf1.reset();
    tex1.reset();

    auto buf2 = take(pool->acquireBuffer(bDesc), "transient acquireBuffer 2 (hit)", totals);
    auto tex2 = take(pool->acquireTexture(tDesc), "transient acquireTexture 2 (hit)", totals);
    check(buf2.get() == rawBuf1 && tex2.get() == rawTex1,
          "TransientResourcePool must reuse recycled Buffer and Texture", totals);

    take(cmd->begin(), "transient begin 2", totals);
    take(cmd->transition(tex2, gfx::ResourceState::RenderTarget), "tex2 -> RT", totals);
    take(cmd->beginRenderPass(tex2, {1.0f, 0.0f, 0.5f, 1.0f}), "clear tex2 color2", totals);
    take(cmd->endRenderPass(), "end pass 2", totals);
    take(cmd->transition(tex2, gfx::ResourceState::CopySource), "tex2 -> CopySrc", totals);
    take(cmd->copyTextureToBuffer(tex2, rb), "copy tex2 -> rb", totals);
    take(cmd->end(), "transient end 2", totals);
    auto f2 = take(device.queue().submit(*cmd), "submit transient 2", totals);
    take(device.queue().wait(f2, std::chrono::seconds(10)), "wait transient 2", totals);
    auto pixels = take(rb->read(0, fp.size), "readback reused tex2", totals);
    check(static_cast<int>(pixels[0]) == 255 && static_cast<int>(pixels[1]) == 0 &&
          std::abs(static_cast<int>(pixels[2]) - 128) <= 1 && static_cast<int>(pixels[3]) == 255,
          "reused transient texture rendered exact second color", totals);

    take(pool->recycle(std::move(buf2), f2), "recycle buf2", totals);
    take(pool->recycle(std::move(tex2), f2), "recycle tex2", totals);

    // 2. Live OS VRAM budget pressure query.
    auto osReport = take(pool->applyBudgetPressure(), "live applyBudgetPressure()", totals);
    if (backend == gfx::Backend::D3D12) {
        check(osReport.level != gfx::BudgetPressureLevel::Unavailable && osReport.budgetBytes > 0,
              "D3D12 live applyBudgetPressure must report real OS VRAM budget", totals);
    } else {
        check(osReport.level == gfx::BudgetPressureLevel::Unavailable,
              "Vulkan live applyBudgetPressure must honestly report Unavailable", totals);
    }

    // 3. Deterministic Soft / Hard budget pressure eviction policy (4 x 256 KiB = 1 MiB idle).
    (void)take(pool->trim(0), "trim before budget test", totals);
    std::vector<gfx::BufferPtr> hold;
    for (int i = 0; i < 4; ++i) {
        hold.push_back(take(pool->acquireBuffer({256u * 1024u, gfx::Memory::Device}), "alloc 256K for budget", totals));
    }
    for (auto& b : hold) take(pool->recycle(std::move(b)), "recycle 256K for budget", totals);
    hold.clear();

    auto pStats = take(pool->statistics(), "stats before pressure", totals);
    check(pStats.pooledCount == 4u && pStats.pooledBytes == 1024u * 1024u, "4 x 256K idle in pool", totals);

    auto normalRep = take(pool->applyBudgetPressure(500u, 1000u), "Normal pressure (50%)", totals);
    check(normalRep.level == gfx::BudgetPressureLevel::Normal && normalRep.evictedResources == 0,
          "50% budget keeps all idle resources", totals);

    auto softRep = take(pool->applyBudgetPressure(850u, 1000u), "Soft pressure (85%)", totals);
    check(softRep.level == gfx::BudgetPressureLevel::Soft && softRep.evictedResources == 2u &&
          softRep.freedBytes == 512u * 1024u,
          "85% Soft pressure trims 1 MiB down to 512 KiB target (2 evicted)", totals);

    auto hardRep = take(pool->applyBudgetPressure(950u, 1000u), "Hard pressure (95%)", totals);
    check(hardRep.level == gfx::BudgetPressureLevel::Hard && hardRep.evictedResources == 2u &&
          hardRep.freedBytes == 512u * 1024u,
          "95% Hard pressure trims all remaining idle resources to 0", totals);

    pStats = take(pool->statistics(), "stats after Hard pressure", totals);
    check(pStats.pooledCount == 0 && pStats.pooledBytes == 0, "pool empty after Hard pressure", totals);

    // Foreign/null rejections.
    reject(pool->acquireBuffer({0, gfx::Memory::Device}), gfx::ErrorCode::InvalidArgument, "acquireBuffer(0) rejected", totals);
    reject(pool->recycle(gfx::BufferPtr{}), gfx::ErrorCode::InvalidArgument, "recycle null buf rejected", totals);
    reject(pool->recycle(gfx::TexturePtr{}), gfx::ErrorCode::InvalidArgument, "recycle null tex rejected", totals);
    auto extBuf = take(device.createBuffer({256, gfx::Memory::Device}), "ext buf", totals);
    auto extTex = take(device.createTexture({16, 16, gfx::Format::RGBA8UNorm}), "ext tex", totals);
    reject(pool->recycle(extBuf), gfx::ErrorCode::WrongDevice, "recycle non-pool buf rejected", totals);
    reject(pool->recycle(extTex), gfx::ErrorCode::WrongDevice, "recycle non-pool tex rejected", totals);
}

void runBenchmark(gfx::Device& device, Totals& totals) {
    constexpr std::uint32_t kFrames = 24;
    constexpr std::uint32_t kUploadsPerFrame = 16;
    constexpr std::uint64_t kUploadChunk = 16u * 1024u; // 16 KiB x 16 = 256 KiB/frame (6 MiB/run)
    constexpr std::uint32_t kTexDim = 256;
    constexpr std::uint32_t kWarmup = 1;
    constexpr std::uint32_t kRuns = 5;

    std::vector<std::byte> payload(kUploadChunk, std::byte{0x5Au});
    auto dstDevBuf = take(device.createBuffer({kUploadsPerFrame * kUploadChunk, gfx::Memory::Device}), "bench dstDevBuf", totals);
    auto cmdPool = take(device.createCommandPool(), "bench cmdPool", totals);
    auto cmd = take(cmdPool->createCommandList(), "bench cmd", totals);
    const bool hasTs = device.caps().timestampQueries;

    struct BenchResult {
        double wallMs{};
        double gpuMs{};
        std::uint64_t peakUniqueResources{};
        std::uint64_t peakAllocBytes{};
        std::uint64_t poolHits{};
        std::uint64_t poolMisses{};
    };

    auto runBaseline = [&]() -> BenchResult {
        BenchResult res{};
        double gpuSum = 0.0;
        const auto t0 = std::chrono::steady_clock::now();
        for (std::uint32_t f = 0; f < kFrames; ++f) {
            take(cmdPool->reset(), "base reset", totals);
            std::vector<gfx::BufferPtr> frameUps;
            frameUps.reserve(kUploadsPerFrame);
            for (std::uint32_t u = 0; u < kUploadsPerFrame; ++u) {
                auto b = take(device.createBuffer({kUploadChunk, gfx::Memory::Upload}), "base up", totals);
                take(b->write(0, payload), "base write", totals);
                frameUps.push_back(std::move(b));
            }
            auto rt = take(device.createTexture({kTexDim, kTexDim, gfx::Format::RGBA8UNorm, true}), "base rt", totals);
            auto rb = take(device.createBuffer({rt->footprint().size, gfx::Memory::Readback}), "base rb", totals);

            const auto st = take(device.resourceStatistics(), "base stats", totals);
            res.peakUniqueResources = std::max(res.peakUniqueResources, st.uniqueResources);
            res.peakAllocBytes = std::max(res.peakAllocBytes, st.resourceAllocationBytes);

            take(cmd->begin(), "base begin", totals);
            if (hasTs) take(cmd->writeTimestamp(0), "base ts0", totals);
            take(cmd->transition(dstDevBuf, gfx::ResourceState::CopyDestination), "base dst -> CopyDst", totals);
            for (std::uint32_t u = 0; u < kUploadsPerFrame; ++u) {
                take(cmd->copyBufferRegion(frameUps[u], 0, dstDevBuf, u * kUploadChunk, kUploadChunk), "base copy", totals);
            }
            take(cmd->transition(rt, gfx::ResourceState::RenderTarget), "base rt -> RT", totals);
            take(cmd->beginRenderPass(rt, {0.2f, 0.4f, 0.6f, 1.0f}), "base pass", totals);
            take(cmd->endRenderPass(), "base endPass", totals);
            take(cmd->transition(rt, gfx::ResourceState::CopySource), "base rt -> CopySrc", totals);
            take(cmd->copyTextureToBuffer(rt, rb), "base copyTex", totals);
            if (hasTs) {
                take(cmd->writeTimestamp(1), "base ts1", totals);
                take(cmd->resolveTimestamps(), "base resolveTs", totals);
            }
            take(cmd->end(), "base end", totals);
            auto fence = take(device.queue().submit(*cmd), "base submit", totals);
            take(device.queue().wait(fence, std::chrono::seconds(30)), "base wait", totals);
            if (hasTs) gpuSum += take(cmd->elapsedGpuMilliseconds(), "base gpuMs", totals);
            auto sample = take(rb->read(0, 4), "base rb read", totals);
            check(static_cast<int>(sample[3]) == 255, "base pixel alpha", totals);
        }
        const auto t1 = std::chrono::steady_clock::now();
        res.wallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        res.gpuMs = gpuSum;
        return res;
    };

    auto runPooled = [&]() -> BenchResult {
        auto ring = take(device.createUploadRing({3u * 512u * 1024u, 256u, 3u, gfx::QueueType::Graphics}), "bench ring", totals);
        auto rbPool = take(device.createReadbackPool({8u * 1024u * 1024u, 8u, gfx::QueueType::Graphics}), "bench rbPool", totals);
        auto trPool = take(device.createTransientResourcePool({16u * 1024u * 1024u, 16u, gfx::QueueType::Graphics, {}}), "bench trPool", totals);

        BenchResult res{};
        double gpuSum = 0.0;
        const auto t0 = std::chrono::steady_clock::now();
        for (std::uint32_t f = 0; f < kFrames; ++f) {
            take(cmdPool->reset(), "pool reset", totals);
            std::vector<gfx::UploadAllocation> allocs;
            allocs.reserve(kUploadsPerFrame);
            for (std::uint32_t u = 0; u < kUploadsPerFrame; ++u) {
                allocs.push_back(take(ring->upload(payload, 256), "pool upload", totals));
            }
            auto rt = take(trPool->acquireTexture({kTexDim, kTexDim, gfx::Format::RGBA8UNorm, true}), "pool rt", totals);
            auto rb = take(rbPool->acquire(rt->footprint().size), "pool rb", totals);

            const auto st = take(device.resourceStatistics(), "pool stats", totals);
            res.peakUniqueResources = std::max(res.peakUniqueResources, st.uniqueResources);
            res.peakAllocBytes = std::max(res.peakAllocBytes, st.resourceAllocationBytes);

            take(cmd->begin(), "pool begin", totals);
            if (hasTs) take(cmd->writeTimestamp(0), "pool ts0", totals);
            take(cmd->transition(dstDevBuf, gfx::ResourceState::CopyDestination), "pool dst -> CopyDst", totals);
            for (std::uint32_t u = 0; u < kUploadsPerFrame; ++u) {
                take(cmd->copyBufferRegion(allocs[u].buffer, allocs[u].offset, dstDevBuf, u * kUploadChunk, kUploadChunk), "pool copy", totals);
            }
            take(cmd->transition(rt, gfx::ResourceState::RenderTarget), "pool rt -> RT", totals);
            take(cmd->beginRenderPass(rt, {0.2f, 0.4f, 0.6f, 1.0f}), "pool pass", totals);
            take(cmd->endRenderPass(), "pool endPass", totals);
            take(cmd->transition(rt, gfx::ResourceState::CopySource), "pool rt -> CopySrc", totals);
            take(cmd->copyTextureToBuffer(rt, rb), "pool copyTex", totals);
            if (hasTs) {
                take(cmd->writeTimestamp(1), "pool ts1", totals);
                take(cmd->resolveTimestamps(), "pool resolveTs", totals);
            }
            take(cmd->end(), "pool end", totals);
            auto fence = take(device.queue().submit(*cmd), "pool submit", totals);
            take(ring->commit(fence), "pool commit", totals);
            take(device.queue().wait(fence, std::chrono::seconds(30)), "pool wait", totals);
            if (hasTs) gpuSum += take(cmd->elapsedGpuMilliseconds(), "pool gpuMs", totals);
            auto sample = take(rb->read(0, 4), "pool rb read", totals);
            check(static_cast<int>(sample[3]) == 255, "pool pixel alpha", totals);
            take(trPool->recycle(std::move(rt), fence), "pool recycle rt", totals);
            take(rbPool->recycle(std::move(rb), fence), "pool recycle rb", totals);
        }
        const auto t1 = std::chrono::steady_clock::now();
        const auto trSt = take(trPool->statistics(), "bench trStats", totals);
        const auto rbSt = take(rbPool->statistics(), "bench rbStats", totals);
        res.wallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        res.gpuMs = gpuSum;
        res.poolHits = trSt.hits + rbSt.hits;
        res.poolMisses = trSt.misses + rbSt.misses;
        return res;
    };

    for (std::uint32_t w = 0; w < kWarmup; ++w) {
        (void)runBaseline();
        (void)runPooled();
    }
    std::vector<double> baseWalls, poolWalls, baseGpus, poolGpus;
    BenchResult lastBase{}, lastPool{};
    for (std::uint32_t r = 0; r < kRuns; ++r) {
        lastBase = runBaseline();
        lastPool = runPooled();
        baseWalls.push_back(lastBase.wallMs);
        poolWalls.push_back(lastPool.wallMs);
        baseGpus.push_back(lastBase.gpuMs);
        poolGpus.push_back(lastPool.gpuMs);
    }
    std::sort(baseWalls.begin(), baseWalls.end());
    std::sort(poolWalls.begin(), poolWalls.end());
    std::sort(baseGpus.begin(), baseGpus.end());
    std::sort(poolGpus.begin(), poolGpus.end());
    const double baseWallMed = baseWalls[baseWalls.size() / 2];
    const double poolWallMed = poolWalls[poolWalls.size() / 2];
    const double baseGpuMed = baseGpus[baseGpus.size() / 2];
    const double poolGpuMed = poolGpus[poolGpus.size() / 2];
    std::cout << std::fixed << std::setprecision(3)
              << "BENCHMARK memory_pool frames=" << kFrames
              << " baseline_wall_ms=" << baseWallMed
              << " pooled_wall_ms=" << poolWallMed
              << " wall_speedup=" << (baseWallMed / poolWallMed) << "x"
              << " baseline_gpu_ms=" << baseGpuMed
              << " pooled_gpu_ms=" << poolGpuMed
              << " base_peak_res=" << lastBase.peakUniqueResources
              << " pool_peak_res=" << lastPool.peakUniqueResources
              << " base_peak_bytes=" << lastBase.peakAllocBytes
              << " pool_peak_bytes=" << lastPool.peakAllocBytes
              << " pool_hits=" << lastPool.poolHits
              << " pool_misses=" << lastPool.poolMisses << "\n";
}

void testPostDestroyAndWarpLossRecovery(Totals& totals) {
    // 1. Safe DeviceLost after Device destruction before pools are destroyed.
    {
        gfx::DeviceDesc desc{};
        desc.backend = gfx::Backend::D3D12;
        desc.softwareAdapter = true;
        auto dev = take(gfx::createDevice(desc), "create WARP for destroy-order test", totals);
        auto ring = take(dev->createUploadRing({4096, 256, 2}), "ring before destroy", totals);
        auto rbPool = take(dev->createReadbackPool({}), "rbPool before destroy", totals);
        auto trPool = take(dev->createTransientResourcePool({}), "trPool before destroy", totals);
        dev.reset();
        reject(ring->allocate(256), gfx::ErrorCode::DeviceLost, "ring->allocate after Device destroy", totals);
        reject(rbPool->acquire(256), gfx::ErrorCode::DeviceLost, "rbPool->acquire after Device destroy", totals);
        reject(trPool->acquireBuffer({256, gfx::Memory::Device}), gfx::ErrorCode::DeviceLost,
               "trPool->acquireBuffer after Device destroy", totals);
    }

    // 2. WARP removeForTesting() -> DeviceLost across all 3 pools -> recover() -> clean reuse!
    {
        gfx::DeviceDesc desc{};
        desc.backend = gfx::Backend::D3D12;
        desc.softwareAdapter = true;
        desc.enableDeviceLossTesting = true;
        auto dev = take(gfx::createDevice(desc), "create WARP for loss/recovery test", totals);
        auto ring = take(dev->createUploadRing({4096, 256, 2}), "WARP ring", totals);
        auto rbPool = take(dev->createReadbackPool({}), "WARP rbPool", totals);
        auto trPool = take(dev->createTransientResourcePool({}), "WARP trPool", totals);

        take(dev->removeForTesting(), "WARP removeForTesting", totals);
        reject(ring->allocate(256), gfx::ErrorCode::DeviceLost, "ring->allocate after loss", totals);
        reject(ring->reclaim(), gfx::ErrorCode::DeviceLost, "ring->reclaim after loss", totals);
        reject(rbPool->acquire(256), gfx::ErrorCode::DeviceLost, "rbPool->acquire after loss", totals);
        reject(trPool->acquireTexture({16, 16, gfx::Format::RGBA8UNorm, true}), gfx::ErrorCode::DeviceLost,
               "trPool->acquireTexture after loss", totals);
        reject(trPool->applyBudgetPressure(), gfx::ErrorCode::DeviceLost, "trPool->applyBudgetPressure after loss", totals);

        auto recovered = take(dev->recover(), "WARP recover", totals);
        testUploadRingLifecycleAndGpuVerification(*recovered, totals);
        testReadbackPoolLifecycle(*recovered, totals);
        testTransientPoolAndBudgetPressure(*recovered, gfx::Backend::D3D12, totals);
    }
}

void verifyEmptyStatistics(gfx::Device& device, Totals& totals) {
    take(take(device.queue(gfx::QueueType::Graphics), "drain gfx", totals)->waitIdle(), "gfx idle", totals);
    take(take(device.queue(gfx::QueueType::Compute), "drain cmp", totals)->waitIdle(), "cmp idle", totals);
    take(take(device.queue(gfx::QueueType::Copy), "drain cpy", totals)->waitIdle(), "cpy idle", totals);
    const auto stats = take(device.resourceStatistics(), "resourceStatistics after cleanup", totals);
    check(stats.trackedObjects == 0, "trackedObjects must return to 0", totals);
    check(stats.uniqueResources == 0, "uniqueResources must return to 0", totals);
    check(stats.pendingSubmissions == 0, "pendingSubmissions must return to 0", totals);
    check(stats.pendingObjectReferences == 0, "pendingObjectReferences must return to 0", totals);
    check(stats.resourceAllocationBytes == 0, "resourceAllocationBytes must return to 0", totals);
}
} // namespace

int main(int argc, char** argv) {
    gfx::Backend backend = gfx::Backend::Vulkan;
    bool validation = false;
    bool warp = false;
    bool benchmark = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--d3d12") backend = gfx::Backend::D3D12;
        else if (arg == "--vulkan") backend = gfx::Backend::Vulkan;
        else if (arg == "--validation") validation = true;
        else if (arg == "--warp") warp = true;
        else if (arg == "--benchmark") benchmark = true;
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return 2;
        }
    }

    Totals totals{};
    try {
        testCpuDescriptorValidation(totals);

        gfx::DeviceDesc desc{};
        desc.backend = backend;
        desc.validation = validation;
        desc.softwareAdapter = warp;
        auto created = gfx::createDevice(desc);
        if (!created) {
            if (created.error().code == gfx::ErrorCode::Unsupported) {
                std::cout << "SKIP memory_pool: " << created.error().message << "\n";
                return 77;
            }
            throw std::runtime_error("createDevice failed: " + created.error().message);
        }
        auto device = std::move(*created);
        std::cout << "MEMORY_POOL adapter=\"" << device->caps().adapterName
                  << "\" backend=" << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
                  << " validation=" << (validation ? 1 : 0)
                  << " warp=" << (warp ? 1 : 0) << "\n";

        testUploadRingLifecycleAndGpuVerification(*device, totals);
        testReadbackPoolLifecycle(*device, totals);
        testTransientPoolAndBudgetPressure(*device, backend, totals);

        if (benchmark) {
            runBenchmark(*device, totals);
        }

        verifyEmptyStatistics(*device, totals);
        if (validation) {
            const auto errs = take(device->validationErrorCount(), "validationErrorCount", totals);
            if (errs != 0) {
                throw std::runtime_error("Validation errors observed: " + std::to_string(errs));
            }
        }
        device.reset();

        if (backend == gfx::Backend::D3D12 && warp && !validation) {
            testPostDestroyAndWarpLossRecovery(totals);
        }

        std::cout << "PASS memory_pool assertions=" << totals.assertions
                  << " frames=" << totals.frames
                  << " rejections=" << totals.rejections << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL memory_pool: " << e.what() << "\n";
        return 1;
    }
}
