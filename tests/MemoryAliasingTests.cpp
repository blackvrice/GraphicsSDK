#include <graphics/Graphics.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct Totals {
    std::uint64_t assertions{};
    std::uint64_t rejections{};
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

std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1u) / alignment * alignment;
}

void testCpuContracts(Totals& totals) {
    // Heap descriptor validation
    reject(gfx::validateMemoryHeapDesc({0u, gfx::MemoryHeapType::Buffers}), gfx::ErrorCode::InvalidArgument, "zero heap size", totals);
    reject(gfx::validateMemoryHeapDesc({4096u, gfx::MemoryHeapType::Buffers}), gfx::ErrorCode::InvalidArgument, "sub-64KiB heap size", totals);
    reject(gfx::validateMemoryHeapDesc({65536u + 16u, gfx::MemoryHeapType::Textures}), gfx::ErrorCode::InvalidArgument, "unaligned heap size", totals);
    reject(gfx::validateMemoryHeapDesc({(4ull << 30) + 65536ull, gfx::MemoryHeapType::RenderTargets}), gfx::ErrorCode::InvalidArgument, "over-4GiB heap size", totals);
    take(gfx::validateMemoryHeapDesc({65536u, gfx::MemoryHeapType::Buffers}), "valid 64KiB heap", totals);
    take(gfx::validateMemoryHeapDesc({262144u, gfx::MemoryHeapType::RenderTargets}), "valid 256KiB heap", totals);

    // Fragmentation stats: empty heap
    {
        const auto s = take(gfx::computeHeapFragmentationStats(262144u, {}, true), "empty heap stats", totals);
        check(s.totalBytes == 262144u && s.allocatedBytes == 0u && s.freeBytes == 262144u, "empty heap bytes", totals);
        check(s.largestFreeBlockBytes == 262144u && s.freeRegionCount == 1u && s.liveAllocations == 0u, "empty heap free region", totals);
        check(s.externalFragmentationRatio == 0.0 && s.aliasedSavedBytes == 0u && s.resident, "empty heap ratio", totals);
    }

    // Fragmentation stats: checkerboard A(0..64K), C(128K..192K) with hole B(64K..128K) & tail(192K..256K)
    {
        const std::array<gfx::HeapAllocationRange, 2> ranges{{{0u, 65536u}, {131072u, 65536u}}};
        const auto s = take(gfx::computeHeapFragmentationStats(262144u, ranges, true), "checkerboard stats", totals);
        check(s.allocatedBytes == 131072u && s.virtualRequestedBytes == 131072u, "checkerboard allocated", totals);
        check(s.freeBytes == 131072u && s.largestFreeBlockBytes == 65536u && s.freeRegionCount == 2u, "checkerboard free holes", totals);
        check(std::fabs(s.externalFragmentationRatio - 0.5) < 1e-9, "checkerboard ratio == 0.5", totals);
        check(s.aliasedSavedBytes == 0u && s.liveAllocations == 2u, "checkerboard live count", totals);
    }

    // Fragmentation stats: full aliasing (two 128KiB resources at offset 0 on 128KiB heap)
    {
        const std::array<gfx::HeapAllocationRange, 2> ranges{{{0u, 131072u}, {0u, 131072u}}};
        const auto s = take(gfx::computeHeapFragmentationStats(131072u, ranges, false), "aliased stats", totals);
        check(s.allocatedBytes == 131072u && s.virtualRequestedBytes == 262144u, "aliased union bytes", totals);
        check(s.aliasedSavedBytes == 131072u && s.freeBytes == 0u && s.externalFragmentationRatio == 0.0, "aliased 50% savings", totals);
        check(s.liveAllocations == 2u && !s.resident, "aliased live count and residency flag", totals);
    }

    // Fragmentation stats invalid range rejection
    {
        const std::array<gfx::HeapAllocationRange, 1> badRange{{{65536u, 65536u}}};
        reject(gfx::computeHeapFragmentationStats(65536u, badRange, true), gfx::ErrorCode::InvalidArgument, "out-of-bounds frag range", totals);
    }
}

void runAdapterSuite(gfx::Backend backend, const gfx::AdapterInfo& adapter, bool validation, Totals& totals) {
    gfx::DeviceDesc desc{};
    desc.backend = backend;
    desc.adapter = adapter.id;
    desc.softwareAdapter = adapter.softwareAdapter;
    desc.validation = validation;

    auto device = take(gfx::createDevice(desc), "createDevice", totals);
    const auto& caps = device->caps();
    check(caps.resourceAliasing, "caps.resourceAliasing must be true", totals);
    if (backend == gfx::Backend::D3D12) {
        check(caps.residencyControl, "D3D12 caps.residencyControl must be true", totals);
    } else if (backend == gfx::Backend::Vulkan) {
        check(!caps.residencyControl, "Vulkan caps.residencyControl must be false", totals);
    }
    if (caps.sparseResidencyTexture2D) {
        check(caps.sparseBinding, "sparseResidencyTexture2D requires sparseBinding", totals);
    }

    // 1. Buffer fragmentation & coalescing measurement
    double measuredFragRatio = 0.0;
    {
        const auto req = take(device->queryBufferMemoryRequirements({65536u, gfx::Memory::Device}), "queryBufferMemoryRequirements(64K)", totals);
        check(req.size >= 65536u && req.alignment >= 1u && req.heapType == gfx::MemoryHeapType::Buffers, "buffer req fields", totals);
        const std::uint64_t slotStride = alignUp(req.size, std::max<std::uint64_t>(req.alignment, 65536u));
        const std::uint64_t heapSize = alignUp(slotStride * 4u, 65536u);
        auto heap = take(device->createMemoryHeap({heapSize, gfx::MemoryHeapType::Buffers}), "createMemoryHeap(frag)", totals);

        auto bufA = take(device->createPlacedBuffer(heap, 0u, {65536u, gfx::Memory::Device}), "placed bufA", totals);
        auto bufB = take(device->createPlacedBuffer(heap, slotStride, {65536u, gfx::Memory::Device}), "placed bufB", totals);
        auto bufC = take(device->createPlacedBuffer(heap, slotStride * 2u, {65536u, gfx::Memory::Device}), "placed bufC", totals);

        const auto fullStats = take(heap->fragmentationStats(), "frag stats 3 live", totals);
        check(fullStats.liveAllocations == 3u && fullStats.aliasedSavedBytes == 0u, "3 live buffers", totals);

        // Free middle block B to create external fragmentation
        bufB.reset();
        const auto holeStats = take(heap->fragmentationStats(), "frag stats checkerboard", totals);
        check(holeStats.liveAllocations == 2u, "2 live buffers after freeing middle", totals);
        check(holeStats.freeRegionCount == 2u, "2 disjoint free regions", totals);
        check(holeStats.largestFreeBlockBytes < holeStats.freeBytes, "largest free block < total free bytes", totals);
        check(holeStats.externalFragmentationRatio > 0.45 && holeStats.externalFragmentationRatio < 0.55, "external fragmentation ratio ~0.50", totals);
        measuredFragRatio = holeStats.externalFragmentationRatio;

        // Free remaining blocks -> coalesces back to 0.0
        bufA.reset();
        bufC.reset();
        const auto emptyStats = take(heap->fragmentationStats(), "frag stats coalesced", totals);
        check(emptyStats.liveAllocations == 0u && emptyStats.allocatedBytes == 0u, "coalesced to 0 allocated", totals);
        check(emptyStats.freeRegionCount == 1u && emptyStats.largestFreeBlockBytes == heapSize, "coalesced single free region", totals);
        check(emptyStats.externalFragmentationRatio == 0.0, "coalesced fragmentation == 0.0", totals);
    }

    // 2. Buffer aliasing + byte-exact GPU execution + VRAM savings + in-flight drop safety
    std::uint64_t bufSavedBytes = 0;
    std::uint64_t bufPhysicalBytes = 0;
    {
        constexpr std::uint64_t kBytes = 131072u; // 128 KiB
        const auto req = take(device->queryBufferMemoryRequirements({kBytes, gfx::Memory::Device}), "queryBufferMemoryRequirements(128K)", totals);
        const std::uint64_t heapSize = alignUp(req.size, 65536u);
        auto heap = take(device->createMemoryHeap({heapSize, gfx::MemoryHeapType::Buffers}), "createMemoryHeap(alias buf)", totals);

        auto bufA = take(device->createPlacedBuffer(heap, 0u, {kBytes, gfx::Memory::Device}), "aliased bufA", totals);
        auto bufB = take(device->createPlacedBuffer(heap, 0u, {kBytes, gfx::Memory::Device}), "aliased bufB", totals);

        const auto aliasStats = take(heap->fragmentationStats(), "alias buffer frag stats", totals);
        check(aliasStats.liveAllocations == 2u, "2 aliased buffers live", totals);
        check(aliasStats.virtualRequestedBytes == req.size * 2u, "virtualRequested == 2 * req.size", totals);
        check(aliasStats.allocatedBytes == req.size, "physical union == req.size", totals);
        check(aliasStats.aliasedSavedBytes == req.size, "50% aliased savings on buffers", totals);
        bufSavedBytes = aliasStats.aliasedSavedBytes;

        const auto devStats = take(device->resourceStatistics(), "resourceStatistics(aliased buffers)", totals);
        check(devStats.uniqueResources == 1u, "aliased buffers share 1 physical MemoryHeap", totals);
        check(devStats.resourceAllocationBytes == heapSize, "device allocation bytes == single heap size", totals);
        bufPhysicalBytes = devStats.resourceAllocationBytes;

        auto uploadA = take(device->createBuffer({kBytes, gfx::Memory::Upload}), "uploadA", totals);
        auto uploadB = take(device->createBuffer({kBytes, gfx::Memory::Upload}), "uploadB", totals);
        auto readbackA = take(device->createBuffer({kBytes, gfx::Memory::Readback}), "readbackA", totals);
        auto readbackB = take(device->createBuffer({kBytes, gfx::Memory::Readback}), "readbackB", totals);

        std::vector<std::byte> patternA(kBytes);
        std::vector<std::byte> patternB(kBytes);
        for (std::size_t i = 0; i < kBytes; ++i) {
            patternA[i] = static_cast<std::byte>((i * 7u + 13u) & 0xffu);
            patternB[i] = static_cast<std::byte>(((i * 11u) ^ 0xA5u) & 0xffu);
        }
        take(uploadA->write(0u, patternA), "write uploadA", totals);
        take(uploadB->write(0u, patternB), "write uploadB", totals);

        auto cmd = take(device->createCommandList(), "createCommandList(buf alias)", totals);
        take(cmd->begin(), "cmd begin", totals);
        take(cmd->transition(bufA, gfx::ResourceState::CopyDestination), "bufA -> CopyDst", totals);
        take(cmd->copyBuffer(uploadA, bufA, kBytes), "copy uploadA -> bufA", totals);
        take(cmd->transition(bufA, gfx::ResourceState::CopySource), "bufA -> CopySrc", totals);
        take(cmd->copyBuffer(bufA, readbackA, kBytes), "copy bufA -> readbackA", totals);

        // Non-overlapping lifetime transition from bufA to bufB on the exact same physical memory
        take(cmd->aliasingBarrier(bufA, bufB), "aliasingBarrier(bufA, bufB)", totals);
        take(cmd->transition(bufB, gfx::ResourceState::CopyDestination), "bufB -> CopyDst", totals);
        take(cmd->copyBuffer(uploadB, bufB, kBytes), "copy uploadB -> bufB", totals);
        take(cmd->transition(bufB, gfx::ResourceState::CopySource), "bufB -> CopySrc", totals);
        take(cmd->copyBuffer(bufB, readbackB, kBytes), "copy bufB -> readbackB", totals);
        take(cmd->end(), "cmd end", totals);

        const auto fence = take(device->queue().submit(*cmd), "submit aliased buffer list", totals);
        take(device->queue().wait(fence, std::chrono::seconds(10)), "wait aliased buffer list", totals);

        const auto gotA = take(readbackA->read(0u, kBytes), "readbackA", totals);
        const auto gotB = take(readbackB->read(0u, kBytes), "readbackB", totals);
        check(gotA == patternA, "aliased bufA byte-exact match", totals);
        check(gotB == patternB, "aliased bufB byte-exact match", totals);

        // Wildcard aliasingBarrier(nullptr, bufA) + drop heap & buffers while submitted
        take(cmd->begin(), "cmd begin wildcard", totals);
        take(cmd->aliasingBarrier(gfx::BufferPtr{}, bufA), "aliasingBarrier(nullptr, bufA)", totals);
        take(cmd->transition(bufA, gfx::ResourceState::CopyDestination), "bufA -> CopyDst 2", totals);
        take(cmd->copyBuffer(uploadB, bufA, kBytes), "copy uploadB -> bufA", totals);
        take(cmd->transition(bufA, gfx::ResourceState::CopySource), "bufA -> CopySrc 2", totals);
        take(cmd->copyBuffer(bufA, readbackA, kBytes), "copy bufA -> readbackA 2", totals);
        take(cmd->end(), "cmd end wildcard", totals);
        const auto fence2 = take(device->queue().submit(*cmd), "submit wildcard alias", totals);
        bufA.reset();
        bufB.reset();
        heap.reset();
        take(device->queue().wait(fence2, std::chrono::seconds(10)), "wait after dropping heap/buffers", totals);
        const auto gotA2 = take(readbackA->read(0u, kBytes), "readbackA2", totals);
        check(gotA2 == patternB, "wildcard aliased bufA byte-exact match after drop", totals);
    }

    const auto afterBufCleanup = take(device->resourceStatistics(), "resourceStatistics after buf cleanup", totals);
    check(afterBufCleanup.uniqueResources == 0u && afterBufCleanup.resourceAllocationBytes == 0u,
          "all buffer heap resources released cleanly", totals);

    // 3. RenderTarget texture aliasing (non-overlapping lifetimes + pixel-exact verification + VRAM savings)
    std::uint64_t rtVirtualBytes = 0;
    std::uint64_t rtPhysicalBytes = 0;
    std::uint64_t rtSavedBytes = 0;
    {
        const gfx::TextureDesc rtDescA{256u, 256u, gfx::Format::RGBA8UNorm, true};
        const gfx::TextureDesc rtDescB{256u, 256u, gfx::Format::RGBA8UNorm, true};
        const auto reqA = take(device->queryTextureMemoryRequirements(rtDescA), "queryTextureMemoryRequirements(rtA)", totals);
        const auto reqB = take(device->queryTextureMemoryRequirements(rtDescB), "queryTextureMemoryRequirements(rtB)", totals);
        check(reqA.size >= 256u * 256u * 4u && reqA.heapType == gfx::MemoryHeapType::RenderTargets, "rtA req", totals);
        check(reqB.size >= 256u * 256u * 4u && reqB.heapType == gfx::MemoryHeapType::RenderTargets, "rtB req", totals);

        const std::uint64_t heapSize = alignUp(std::max(reqA.size, reqB.size), 65536u);
        auto rtHeap = take(device->createMemoryHeap({heapSize, gfx::MemoryHeapType::RenderTargets}), "createMemoryHeap(RT)", totals);

        auto rtA = take(device->createPlacedTexture(rtHeap, 0u, rtDescA), "placed rtA", totals);
        auto rtB = take(device->createPlacedTexture(rtHeap, 0u, rtDescB), "placed rtB", totals);

        const auto rtFrag = take(rtHeap->fragmentationStats(), "rtHeap fragmentationStats", totals);
        check(rtFrag.liveAllocations == 2u, "2 aliased RTs live", totals);
        check(rtFrag.virtualRequestedBytes == reqA.size + reqB.size, "RT virtualRequested == reqA + reqB", totals);
        check(rtFrag.aliasedSavedBytes == std::min(reqA.size, reqB.size), "RT aliasedSavedBytes == 50%", totals);
        rtVirtualBytes = rtFrag.virtualRequestedBytes;
        rtSavedBytes = rtFrag.aliasedSavedBytes;

        const auto rtDevStats = take(device->resourceStatistics(), "resourceStatistics(aliased RTs)", totals);
        check(rtDevStats.uniqueResources == 1u && rtDevStats.resourceAllocationBytes == heapSize,
              "2 aliased RTs backed by 1 physical heap", totals);
        rtPhysicalBytes = rtDevStats.resourceAllocationBytes;

        const auto fpA = rtA->footprint();
        const auto fpB = rtB->footprint();
        auto rbA = take(device->createBuffer({fpA.size, gfx::Memory::Readback}), "rbA", totals);
        auto rbB = take(device->createBuffer({fpB.size, gfx::Memory::Readback}), "rbB", totals);

        auto cmd = take(device->createCommandList(), "createCommandList(RT alias)", totals);
        take(cmd->begin(), "RT cmd begin", totals);
        // Lifetime A: clear rtA to red and copy out
        take(cmd->transition(rtA, gfx::ResourceState::RenderTarget), "rtA -> RT", totals);
        take(cmd->beginRenderPass(rtA, gfx::Color{1.0f, 0.0f, 0.0f, 1.0f}), "pass rtA red", totals);
        take(cmd->endRenderPass(), "end pass rtA", totals);
        take(cmd->transition(rtA, gfx::ResourceState::CopySource), "rtA -> CopySrc", totals);
        take(cmd->copyTextureToBuffer(rtA, rbA), "copy rtA -> rbA", totals);

        // Aliasing barrier: hand physical memory from rtA to rtB
        take(cmd->aliasingBarrier(rtA, rtB), "aliasingBarrier(rtA, rtB)", totals);

        // Lifetime B: clear rtB to green and copy out
        take(cmd->transition(rtB, gfx::ResourceState::RenderTarget), "rtB -> RT", totals);
        take(cmd->beginRenderPass(rtB, gfx::Color{0.0f, 1.0f, 0.0f, 1.0f}), "pass rtB green", totals);
        take(cmd->endRenderPass(), "end pass rtB", totals);
        take(cmd->transition(rtB, gfx::ResourceState::CopySource), "rtB -> CopySrc", totals);
        take(cmd->copyTextureToBuffer(rtB, rbB), "copy rtB -> rbB", totals);
        take(cmd->end(), "RT cmd end", totals);

        const auto fence = take(device->queue().submit(*cmd), "submit RT alias", totals);
        take(device->queue().wait(fence, std::chrono::seconds(10)), "wait RT alias", totals);

        const auto pixelsA = take(rbA->read(0u, fpA.size), "read rbA", totals);
        const auto pixelsB = take(rbB->read(0u, fpB.size), "read rbB", totals);
        for (std::uint32_t y = 0; y < 256u; ++y) {
            const std::size_t rowA = static_cast<std::size_t>(y) * fpA.rowPitch;
            const std::size_t rowB = static_cast<std::size_t>(y) * fpB.rowPitch;
            for (std::uint32_t x = 0; x < 256u; ++x) {
                const std::size_t pxA = rowA + static_cast<std::size_t>(x) * 4u;
                const std::size_t pxB = rowB + static_cast<std::size_t>(x) * 4u;
                if (static_cast<std::uint8_t>(pixelsA[pxA + 0]) != 255u ||
                    static_cast<std::uint8_t>(pixelsA[pxA + 1]) != 0u ||
                    static_cast<std::uint8_t>(pixelsA[pxA + 2]) != 0u ||
                    static_cast<std::uint8_t>(pixelsA[pxA + 3]) != 255u) {
                    throw std::runtime_error("Aliased rtA pixel mismatch");
                }
                if (static_cast<std::uint8_t>(pixelsB[pxB + 0]) != 0u ||
                    static_cast<std::uint8_t>(pixelsB[pxB + 1]) != 255u ||
                    static_cast<std::uint8_t>(pixelsB[pxB + 2]) != 0u ||
                    static_cast<std::uint8_t>(pixelsB[pxB + 3]) != 255u) {
                    throw std::runtime_error("Aliased rtB pixel mismatch");
                }
            }
        }
        ++totals.assertions;
    }

    const auto afterRtCleanup = take(device->resourceStatistics(), "resourceStatistics after RT cleanup", totals);
    check(afterRtCleanup.uniqueResources == 0u && afterRtCleanup.resourceAllocationBytes == 0u,
          "all RT heap resources released cleanly", totals);

    // 4. Residency control (Evict / MakeResident on D3D12; explicit Unsupported on Vulkan)
    {
        auto heap = take(device->createMemoryHeap({65536u, gfx::MemoryHeapType::Buffers}), "createMemoryHeap(residency)", totals);
        check(take(heap->isResident(), "initial isResident", totals) == true, "heap starts resident", totals);
        if (caps.residencyControl) {
            auto placed = take(device->createPlacedBuffer(heap, 0u, {4096u, gfx::Memory::Device}), "placed for residency", totals);
            auto upload = take(device->createBuffer({4096u, gfx::Memory::Upload}), "res upload", totals);
            auto readback = take(device->createBuffer({4096u, gfx::Memory::Readback}), "res readback", totals);
            std::vector<std::byte> payload(4096u, static_cast<std::byte>(0x5Au));
            take(upload->write(0u, payload), "write residency upload", totals);

            auto cmd = take(device->createCommandList(), "cmd residency", totals);
            take(cmd->begin(), "res cmd begin", totals);
            take(cmd->transition(placed, gfx::ResourceState::CopyDestination), "placed -> CopyDst", totals);
            take(cmd->copyBuffer(upload, placed, 4096u), "copy upload -> placed", totals);
            take(cmd->transition(placed, gfx::ResourceState::CopySource), "placed -> CopySrc", totals);
            take(cmd->copyBuffer(placed, readback, 4096u), "copy placed -> readback", totals);
            take(cmd->end(), "res cmd end", totals);

            take(heap->setResident(false), "evict idle heap", totals);
            check(take(heap->isResident(), "evicted isResident", totals) == false, "heap reports evicted", totals);
            check(!take(heap->fragmentationStats(), "evicted frag stats", totals).resident, "frag stats reports evicted", totals);
            reject(device->createPlacedBuffer(heap, 0u, {4096u, gfx::Memory::Device}), gfx::ErrorCode::InvalidState, "createPlacedBuffer on evicted heap", totals);
            reject(device->queue().submit(*cmd), gfx::ErrorCode::InvalidState, "submit referencing evicted heap", totals);

            take(heap->setResident(true), "makeResident heap", totals);
            check(take(heap->isResident(), "re-resident isResident", totals) == true, "heap reports resident again", totals);
            const auto fence = take(device->queue().submit(*cmd), "submit after MakeResident", totals);
            take(device->queue().wait(fence, std::chrono::seconds(10)), "wait after MakeResident", totals);
            const auto out = take(readback->read(0u, 4096u), "readback after MakeResident", totals);
            check(out == payload, "residency restored payload matches", totals);
        } else {
            reject(heap->setResident(false), gfx::ErrorCode::Unsupported, "Vulkan setResident unsupported", totals);
            check(take(heap->isResident(), "Vulkan isResident remains true", totals) == true, "Vulkan always resident", totals);
        }
    }

    // 5. Sparse / Virtual Texture property query (zero leaked probe resources)
    gfx::SparseTextureProperties sparseRgba{};
    {
        const gfx::TextureDesc sparseDesc{1024u, 1024u, gfx::Format::RGBA8UNorm, false, false, false, 1u, gfx::TextureDimension::Texture2D, 1u, 11u, 1u};
        sparseRgba = take(device->querySparseTextureProperties(sparseDesc), "querySparseTextureProperties(1024 RGBA8)", totals);
        if (caps.sparseResidencyTexture2D) {
            check(sparseRgba.supported, "1024x1024 RGBA8 sparse residency supported", totals);
            check(sparseRgba.tileWidth > 0u && sparseRgba.tileHeight > 0u && sparseRgba.tileDepth >= 1u, "sparse tile dimensions positive", totals);
            check(sparseRgba.standardTileBytes >= 4096u && sparseRgba.totalTiles > 0u, "sparse tile bytes and count positive", totals);
        } else {
            check(!sparseRgba.supported, "sparse unsupported when cap is false", totals);
        }

        // 1D texture sparse query returns supported=false cleanly without validation errors
        const gfx::TextureDesc tex1d{256u, 1u, gfx::Format::RGBA8UNorm, false, false, false, 1u, gfx::TextureDimension::Texture1D, 1u, 1u, 1u};
        const auto sparse1d = take(device->querySparseTextureProperties(tex1d), "querySparseTextureProperties(1D)", totals);
        check(!sparse1d.supported, "1D texture sparse residency unsupported", totals);

        // BC7 sparse query if BC7 is supported
        const auto bc7Support = take(device->queryFormatSupport(gfx::Format::BC7UNorm), "queryFormatSupport(BC7)", totals);
        if (bc7Support.texture2D && bc7Support.sampled) {
            const gfx::TextureDesc bc7Desc{1024u, 1024u, gfx::Format::BC7UNorm, false, false, false, 1u, gfx::TextureDimension::Texture2D, 1u, 11u, 1u};
            const auto sparseBc7 = take(device->querySparseTextureProperties(bc7Desc), "querySparseTextureProperties(BC7)", totals);
            if (sparseBc7.supported) {
                check(sparseBc7.tileWidth > 0u && sparseBc7.tileHeight > 0u && sparseBc7.totalTiles > 0u, "BC7 sparse tile valid", totals);
            }
        }
    }
    const auto afterSparseStats = take(device->resourceStatistics(), "resourceStatistics after sparse query", totals);
    check(afterSparseStats.uniqueResources == 0u && afterSparseStats.resourceAllocationBytes == 0u,
          "sparse probe leaves zero leaked resources", totals);

    // 6. Negative / Rejection & Cross-Device Contracts
    {
        reject(device->createMemoryHeap({0u, gfx::MemoryHeapType::Buffers}), gfx::ErrorCode::InvalidArgument, "createMemoryHeap(0)", totals);
        reject(device->createMemoryHeap({4096u, gfx::MemoryHeapType::Buffers}), gfx::ErrorCode::InvalidArgument, "createMemoryHeap(4K)", totals);
        reject(device->queryBufferMemoryRequirements({4096u, gfx::Memory::Upload}), gfx::ErrorCode::InvalidArgument, "queryBufferMemoryRequirements(Upload)", totals);

        auto bufHeap = take(device->createMemoryHeap({131072u, gfx::MemoryHeapType::Buffers}), "reject bufHeap", totals);
        auto texHeap = take(device->createMemoryHeap({262144u, gfx::MemoryHeapType::Textures}), "reject texHeap", totals);
        reject(device->createPlacedBuffer(nullptr, 0u, {4096u, gfx::Memory::Device}), gfx::ErrorCode::InvalidArgument, "null heap placed buffer", totals);
        reject(device->createPlacedBuffer(bufHeap, 0u, {4096u, gfx::Memory::Upload}), gfx::ErrorCode::InvalidArgument, "upload placed buffer", totals);
        reject(device->createPlacedBuffer(texHeap, 0u, {4096u, gfx::Memory::Device}), gfx::ErrorCode::InvalidArgument, "buffer on Textures heap", totals);
        reject(device->createPlacedBuffer(bufHeap, 13u, {4096u, gfx::Memory::Device}), gfx::ErrorCode::InvalidArgument, "unaligned buffer offset", totals);
        reject(device->createPlacedBuffer(bufHeap, 131072u, {4096u, gfx::Memory::Device}), gfx::ErrorCode::InvalidArgument, "out-of-bounds buffer offset", totals);

        reject(device->createPlacedTexture(nullptr, 0u, {64u, 64u, gfx::Format::RGBA8UNorm}), gfx::ErrorCode::InvalidArgument, "null heap placed texture", totals);
        reject(device->createPlacedTexture(bufHeap, 0u, {64u, 64u, gfx::Format::RGBA8UNorm}), gfx::ErrorCode::InvalidArgument, "texture on Buffers heap", totals);
        reject(device->createPlacedTexture(texHeap, 0u, {64u, 64u, gfx::Format::RGBA8UNorm, true}), gfx::ErrorCode::InvalidArgument, "RT texture on non-RT Textures heap", totals);
        reject(device->createPlacedTexture(texHeap, 17u, {64u, 64u, gfx::Format::RGBA8UNorm}), gfx::ErrorCode::InvalidArgument, "unaligned texture offset", totals);
        reject(device->createPlacedTexture(texHeap, 262144u, {64u, 64u, gfx::Format::RGBA8UNorm}), gfx::ErrorCode::InvalidArgument, "out-of-bounds texture offset", totals);

        auto committedBuf = take(device->createBuffer({4096u, gfx::Memory::Device}), "committedBuf", totals);
        auto placedBuf = take(device->createPlacedBuffer(bufHeap, 0u, {4096u, gfx::Memory::Device}), "placedBuf", totals);
        auto rt = take(device->createTexture({32u, 32u, gfx::Format::RGBA8UNorm, true}), "committed rt", totals);

        auto cmd = take(device->createCommandList(), "reject cmd", totals);
        take(cmd->begin(), "reject cmd begin", totals);
        reject(cmd->aliasingBarrier(placedBuf, gfx::BufferPtr{}), gfx::ErrorCode::InvalidArgument, "aliasingBarrier null after", totals);
        reject(cmd->aliasingBarrier(placedBuf, placedBuf), gfx::ErrorCode::InvalidArgument, "aliasingBarrier same resource", totals);
        reject(cmd->aliasingBarrier(placedBuf, committedBuf), gfx::ErrorCode::InvalidArgument, "aliasingBarrier committed after", totals);
        reject(cmd->aliasingBarrier(committedBuf, placedBuf), gfx::ErrorCode::InvalidArgument, "aliasingBarrier committed before", totals);
        reject(cmd->aliasingBarrier(gfx::TexturePtr{}, rt), gfx::ErrorCode::InvalidArgument, "aliasingBarrier committed texture", totals);

        take(cmd->transition(rt, gfx::ResourceState::RenderTarget), "rt -> RT for pass reject", totals);
        take(cmd->beginRenderPass(rt, gfx::Color{0, 0, 0, 1}), "begin pass for alias reject", totals);
        reject(cmd->aliasingBarrier(gfx::BufferPtr{}, placedBuf), gfx::ErrorCode::InvalidState, "aliasingBarrier inside render pass", totals);
        take(cmd->endRenderPass(), "end pass", totals);
        take(cmd->end(), "reject cmd end", totals);

        // Cross-device heap rejection
        gfx::DeviceDesc otherDesc = desc;
        otherDesc.validation = false;
        auto otherDevice = take(gfx::createDevice(otherDesc), "create second device for WrongDevice check", totals);
        auto foreignHeap = take(otherDevice->createMemoryHeap({65536u, gfx::MemoryHeapType::Buffers}), "foreign heap", totals);
        reject(device->createPlacedBuffer(foreignHeap, 0u, {4096u, gfx::Memory::Device}), gfx::ErrorCode::WrongDevice, "foreign heap placed buffer", totals);
        reject(device->createPlacedTexture(foreignHeap, 0u, {64u, 64u, gfx::Format::RGBA8UNorm}), gfx::ErrorCode::WrongDevice, "foreign heap placed texture", totals);
    }

    const auto finalStats = take(device->resourceStatistics(), "final resourceStatistics", totals);
    check(finalStats.uniqueResources == 0u && finalStats.resourceAllocationBytes == 0u,
          "final resource leak check == 0", totals);

    std::uint64_t valErrors = 0;
    if (validation) {
        valErrors = take(device->validationErrorCount(), "validationErrorCount", totals);
        check(valErrors == 0u, "validation errors must be 0", totals);
    }

    std::cout << "PASS adapter=\"" << caps.adapterName << "\""
              << " backend=" << (backend == gfx::Backend::D3D12 ? "D3D12" : "Vulkan")
              << " validation=" << (validation ? "on" : "off")
              << " val_errors=" << valErrors
              << " frag_ratio=" << std::fixed << std::setprecision(2) << measuredFragRatio
              << " buf_phys=" << bufPhysicalBytes << " buf_saved=" << bufSavedBytes
              << " rt_virt=" << rtVirtualBytes << " rt_phys=" << rtPhysicalBytes << " rt_saved=" << rtSavedBytes
              << " residency=" << (caps.residencyControl ? "yes" : "no")
              << " sparse2d=" << (sparseRgba.supported ? "yes" : "no");
    if (sparseRgba.supported) {
        std::cout << " sparse_tile=" << sparseRgba.tileWidth << "x" << sparseRgba.tileHeight << "x" << sparseRgba.tileDepth
                  << " tile_bytes=" << sparseRgba.standardTileBytes
                  << " total_tiles=" << sparseRgba.totalTiles
                  << " packed_start=" << sparseRgba.packedMipStart
                  << " packed_count=" << sparseRgba.packedMipCount;
    }
    std::cout << "\n";
}

void runWarpLossRecovery(Totals& totals) {
    gfx::DeviceDesc desc{};
    desc.backend = gfx::Backend::D3D12;
    desc.softwareAdapter = true;
    desc.enableDeviceLossTesting = true;
    auto device = take(gfx::createDevice(desc), "create WARP loss device", totals);
    auto heap = take(device->createMemoryHeap({65536u, gfx::MemoryHeapType::Buffers}), "WARP heap before loss", totals);

    take(device->removeForTesting(), "WARP removeForTesting", totals);
    reject(device->createMemoryHeap({65536u, gfx::MemoryHeapType::Buffers}), gfx::ErrorCode::DeviceLost, "createMemoryHeap after loss", totals);
    reject(device->queryBufferMemoryRequirements({4096u, gfx::Memory::Device}), gfx::ErrorCode::DeviceLost, "queryBufferMemoryRequirements after loss", totals);
    reject(device->querySparseTextureProperties({256u, 256u, gfx::Format::RGBA8UNorm}), gfx::ErrorCode::DeviceLost, "querySparseTextureProperties after loss", totals);
    reject(heap->fragmentationStats(), gfx::ErrorCode::DeviceLost, "heap fragmentationStats after loss", totals);
    reject(heap->setResident(false), gfx::ErrorCode::DeviceLost, "heap setResident after loss", totals);
    reject(heap->isResident(), gfx::ErrorCode::DeviceLost, "heap isResident after loss", totals);

    auto recovered = take(device->recover(), "WARP recover()", totals);
    reject(recovered->createPlacedBuffer(heap, 0u, {4096u, gfx::Memory::Device}), gfx::ErrorCode::WrongDevice, "old generation heap rejected on recovered device", totals);

    auto freshHeap = take(recovered->createMemoryHeap({65536u, gfx::MemoryHeapType::Buffers}), "fresh heap on recovered WARP", totals);
    auto freshBuf = take(recovered->createPlacedBuffer(freshHeap, 0u, {4096u, gfx::Memory::Device}), "fresh placed buf on recovered WARP", totals);
    const auto stats = take(freshHeap->fragmentationStats(), "fresh heap stats on recovered WARP", totals);
    check(stats.liveAllocations == 1u && stats.resident, "recovered WARP placed buffer active", totals);
}
} // namespace

int main(int argc, char** argv) {
    try {
        gfx::Backend backend = gfx::Backend::D3D12;
        bool validation = false;
        bool warp = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--d3d12") backend = gfx::Backend::D3D12;
            else if (arg == "--vulkan") backend = gfx::Backend::Vulkan;
            else if (arg == "--validation") validation = true;
            else if (arg == "--warp") warp = true;
            else {
                std::cerr << "Unknown argument: " << arg << "\n";
                return 2;
            }
        }

        Totals totals{};
        testCpuContracts(totals);

        auto adaptersResult = gfx::enumerateAdapters(backend);
        if (!adaptersResult) {
            if (adaptersResult.error().code == gfx::ErrorCode::Unsupported) {
                std::cout << "SKIP: backend unavailable: " << adaptersResult.error().message << "\n";
                return 77;
            }
            throw std::runtime_error(adaptersResult.error().message);
        }

        std::vector<gfx::AdapterInfo> selected;
        for (const auto& info : *adaptersResult) {
            if (info.softwareAdapter == warp) selected.push_back(info);
        }
        if (selected.empty()) {
            std::cout << "SKIP: no matching adapter found\n";
            return 77;
        }

        for (const auto& adapter : selected) {
            runAdapterSuite(backend, adapter, validation, totals);
        }
        if (backend == gfx::Backend::D3D12 && warp && !validation) {
            runWarpLossRecovery(totals);
        }

        std::cout << "SUMMARY assertions=" << totals.assertions
                  << " rejections=" << totals.rejections
                  << " adapters=" << selected.size() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
