#include <graphics/Graphics.hpp>
#include "fixtures/ComputeFixtures.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Totals {
    std::uint64_t adapters{};
    std::uint64_t assertions{};
    std::uint64_t rejections{};
    std::uint64_t framesVerified{};
};

void check(bool condition, const std::string& message, Totals& totals) {
    if (!condition) throw std::runtime_error(message);
    ++totals.assertions;
}

template <class T>
T take(gfx::Result<T> result, const std::string& what, Totals& totals) {
    if (!result) {
        throw std::runtime_error(what + ": " + result.error().message +
                                 " (code=" + std::to_string(static_cast<int>(result.error().code)) +
                                 ", native=" + std::to_string(result.error().nativeCode) + ")");
    }
    ++totals.assertions;
    return std::move(result.value());
}

void take(gfx::Result<void> result, const std::string& what, Totals& totals) {
    if (!result) {
        throw std::runtime_error(what + ": " + result.error().message +
                                 " (code=" + std::to_string(static_cast<int>(result.error().code)) +
                                 ", native=" + std::to_string(result.error().nativeCode) + ")");
    }
    ++totals.assertions;
}

template <class T>
void expectError(const gfx::Result<T>& result, gfx::ErrorCode expected, const std::string& what, Totals& totals) {
    if (result) throw std::runtime_error(what + ": expected error but succeeded");
    if (result.error().code != expected) {
        throw std::runtime_error(what + ": expected error code " +
                                 std::to_string(static_cast<int>(expected)) + " got " +
                                 std::to_string(static_cast<int>(result.error().code)) +
                                 " (" + result.error().message + ")");
    }
    ++totals.rejections;
}

template <std::size_t N>
gfx::ShaderBinary spirvBinary(gfx::ShaderStage stage, const std::array<std::uint32_t, N>& words) {
    gfx::ShaderBinary bin{};
    bin.format = gfx::ShaderFormat::SPIRV;
    bin.stage = stage;
    bin.bytes.resize(N * sizeof(std::uint32_t));
    std::memcpy(bin.bytes.data(), words.data(), bin.bytes.size());
    return bin;
}

struct VideoParams {
    float yOffset{};
    float yScale{};
    float uvOffset{};
    float uvScale{};
};

gfx::PipelinePtr makeYuvToRgbPipeline(gfx::Device& device, gfx::Backend backend, Totals& totals) {
    gfx::ShaderBinary cs{};
    if (backend == gfx::Backend::D3D12) {
        constexpr std::string_view hlsl = R"(
Texture2D<float> texY : register(t0);
Texture2D<float2> texUV : register(t1);
RWTexture2D<unorm float4> outRgb : register(u0);
cbuffer VideoParams : register(b0) {
    float4 rangeParams;
};
float sat(float x) {
    float a = x < 0.0 ? 0.0 : x;
    return a > 1.0 ? 1.0 : a;
}
[numthreads(8, 8, 1)]
void CS(uint3 gid : SV_DispatchThreadID) {
    int2 p = int2(gid.xy);
    float yRaw = texY.Load(int3(p, 0));
    float2 uvRaw = texUV.Load(int3(p >> 1, 0));
    float y = (yRaw - rangeParams.x) * rangeParams.y;
    float u = (uvRaw.x - rangeParams.z) * rangeParams.w;
    float v = (uvRaw.y - rangeParams.z) * rangeParams.w;
    float r = sat(y + 1.5748 * v);
    float g = sat(y - 0.187324 * u - 0.468124 * v);
    float b = sat(y + 1.8556 * u);
    outRgb[p] = float4(r, g, b, 1.0);
}
)";
        cs = take(gfx::compileHlsl(hlsl, "CS", gfx::ShaderStage::Compute), "compile HLSL YUV->RGB CS", totals);
    } else {
        cs = spirvBinary(gfx::ShaderStage::Compute, computeFixtures::yuv_to_rgb_bt709);
    }

    gfx::ComputePipelineDesc desc{};
    desc.compute = cs;
    desc.bindingLayout.entries = {
        {gfx::BindingType::Texture2D, 0, 1, 0, gfx::ShaderVisibility::Compute},
        {gfx::BindingType::Texture2D, 1, 1, 0, gfx::ShaderVisibility::Compute},
        {gfx::BindingType::StorageTexture2D, 0, 1, 0, gfx::ShaderVisibility::Compute},
        {gfx::BindingType::ConstantBuffer, 0, 1, 0, gfx::ShaderVisibility::Compute},
    };
    return take(device.createComputePipeline(desc), "create YUV->RGB compute pipeline", totals);
}

struct YuvFrameData {
    std::uint32_t width{};
    std::uint32_t height{};
    bool tenBit{};
    std::vector<std::byte> yPlane;
    std::vector<std::byte> uvPlane;
    gfx::TextureFootprint yFp{};
    gfx::TextureFootprint uvFp{};
    VideoParams params{};
};

YuvFrameData buildDeterministicYuvFrame(std::uint32_t width, std::uint32_t height, bool tenBit, std::uint32_t seed, Totals& totals) {
    YuvFrameData frame{};
    frame.width = width;
    frame.height = height;
    frame.tenBit = tenBit;
    const gfx::Format fmt = tenBit ? gfx::Format::P010 : gfx::Format::NV12;
    frame.yFp = take(gfx::computeTexturePlaneFootprint(fmt, width, height, 0), "compute Y plane footprint", totals);
    frame.uvFp = take(gfx::computeTexturePlaneFootprint(fmt, width, height, 1), "compute UV plane footprint", totals);
    frame.yPlane.assign(frame.yFp.size, std::byte{0});
    frame.uvPlane.assign(frame.uvFp.size, std::byte{0});

    if (!tenBit) {
        // BT.709 8-bit limited range: Y in [16..235], UV in [16..240]
        frame.params = {
            16.0f / 255.0f,
            255.0f / 219.0f,
            128.0f / 255.0f,
            255.0f / 224.0f
        };
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::uint8_t yVal = static_cast<std::uint8_t>(16u + ((x * 2u + y + seed * 17u) % 220u));
                frame.yPlane[static_cast<std::size_t>(y) * frame.yFp.rowPitch + x] = static_cast<std::byte>(yVal);
            }
        }
        for (std::uint32_t y = 0; y < height / 2u; ++y) {
            for (std::uint32_t x = 0; x < width / 2u; ++x) {
                const std::uint8_t uVal = static_cast<std::uint8_t>(16u + ((x * 3u + seed * 11u) % 225u));
                const std::uint8_t vVal = static_cast<std::uint8_t>(16u + ((y * 3u + seed * 23u) % 225u));
                const std::size_t off = static_cast<std::size_t>(y) * frame.uvFp.rowPitch + x * 2u;
                frame.uvPlane[off + 0] = static_cast<std::byte>(uVal);
                frame.uvPlane[off + 1] = static_cast<std::byte>(vVal);
            }
        }
    } else {
        // BT.709 10-bit P010 (MSB-aligned in 16-bit UNORM: value10 << 6):
        // Y10 in [64..940], UV10 in [64..960]
        frame.params = {
            static_cast<float>(64u << 6) / 65535.0f,
            65535.0f / static_cast<float>((940u - 64u) << 6),
            static_cast<float>(512u << 6) / 65535.0f,
            65535.0f / static_cast<float>((960u - 64u) << 6)
        };
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::uint16_t y10 = static_cast<std::uint16_t>(64u + ((x * 7u + y * 5u + seed * 31u) % 877u));
                const std::uint16_t y16 = static_cast<std::uint16_t>(y10 << 6);
                const std::size_t off = static_cast<std::size_t>(y) * frame.yFp.rowPitch + x * 2u;
                std::memcpy(frame.yPlane.data() + off, &y16, sizeof(y16));
            }
        }
        for (std::uint32_t y = 0; y < height / 2u; ++y) {
            for (std::uint32_t x = 0; x < width / 2u; ++x) {
                const std::uint16_t u10 = static_cast<std::uint16_t>(64u + ((x * 13u + seed * 19u) % 897u));
                const std::uint16_t v10 = static_cast<std::uint16_t>(64u + ((y * 13u + seed * 29u) % 897u));
                const std::uint16_t u16 = static_cast<std::uint16_t>(u10 << 6);
                const std::uint16_t v16 = static_cast<std::uint16_t>(v10 << 6);
                const std::size_t off = static_cast<std::size_t>(y) * frame.uvFp.rowPitch + x * 4u;
                std::memcpy(frame.uvPlane.data() + off + 0, &u16, sizeof(u16));
                std::memcpy(frame.uvPlane.data() + off + 2, &v16, sizeof(v16));
            }
        }
    }
    return frame;
}

void verifyRgbAgainstCpuBt709(
    const YuvFrameData& frame,
    const std::vector<std::byte>& rgbBytes,
    const gfx::TextureFootprint& rgbFp,
    const std::string& label,
    Totals& totals) {
    auto sat = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            float yRaw = 0.0f, uRaw = 0.0f, vRaw = 0.0f;
            if (!frame.tenBit) {
                const std::uint8_t yb = std::to_integer<std::uint8_t>(
                    frame.yPlane[static_cast<std::size_t>(y) * frame.yFp.rowPitch + x]);
                const std::size_t uvOff = static_cast<std::size_t>(y >> 1) * frame.uvFp.rowPitch + (x >> 1) * 2u;
                const std::uint8_t ub = std::to_integer<std::uint8_t>(frame.uvPlane[uvOff + 0]);
                const std::uint8_t vb = std::to_integer<std::uint8_t>(frame.uvPlane[uvOff + 1]);
                yRaw = static_cast<float>(yb) / 255.0f;
                uRaw = static_cast<float>(ub) / 255.0f;
                vRaw = static_cast<float>(vb) / 255.0f;
            } else {
                std::uint16_t y16 = 0, u16 = 0, v16 = 0;
                std::memcpy(&y16, frame.yPlane.data() + static_cast<std::size_t>(y) * frame.yFp.rowPitch + x * 2u, 2);
                const std::size_t uvOff = static_cast<std::size_t>(y >> 1) * frame.uvFp.rowPitch + (x >> 1) * 4u;
                std::memcpy(&u16, frame.uvPlane.data() + uvOff + 0, 2);
                std::memcpy(&v16, frame.uvPlane.data() + uvOff + 2, 2);
                yRaw = static_cast<float>(y16) / 65535.0f;
                uRaw = static_cast<float>(u16) / 65535.0f;
                vRaw = static_cast<float>(v16) / 65535.0f;
            }
            const float Y = (yRaw - frame.params.yOffset) * frame.params.yScale;
            const float U = (uRaw - frame.params.uvOffset) * frame.params.uvScale;
            const float V = (vRaw - frame.params.uvOffset) * frame.params.uvScale;
            const int er = static_cast<int>(std::lround(sat(Y + 1.5748f * V) * 255.0f));
            const int eg = static_cast<int>(std::lround(sat(Y - 0.187324f * U - 0.468124f * V) * 255.0f));
            const int eb = static_cast<int>(std::lround(sat(Y + 1.8556f * U) * 255.0f));

            const std::size_t outOff = static_cast<std::size_t>(y) * rgbFp.rowPitch + x * 4u;
            const int ar = static_cast<int>(std::to_integer<std::uint8_t>(rgbBytes[outOff + 0]));
            const int ag = static_cast<int>(std::to_integer<std::uint8_t>(rgbBytes[outOff + 1]));
            const int ab = static_cast<int>(std::to_integer<std::uint8_t>(rgbBytes[outOff + 2]));
            const int aa = static_cast<int>(std::to_integer<std::uint8_t>(rgbBytes[outOff + 3]));

            if (std::abs(ar - er) > 2 || std::abs(ag - eg) > 2 || std::abs(ab - eb) > 2 || aa != 255) {
                throw std::runtime_error(label + " pixel mismatch at (" + std::to_string(x) + "," + std::to_string(y) +
                    "): expected (" + std::to_string(er) + "," + std::to_string(eg) + "," + std::to_string(eb) + ",255) got (" +
                    std::to_string(ar) + "," + std::to_string(ag) + "," + std::to_string(ab) + "," + std::to_string(aa) + ")");
            }
        }
    }
    ++totals.assertions;
    ++totals.framesVerified;
}

std::vector<std::byte> runYuvConvertCompute(
    gfx::Device& device,
    const gfx::PipelinePtr& pipeline,
    const gfx::TexturePtr& texYOwner,
    const gfx::TextureViewPtr& viewY,
    const gfx::TexturePtr& texUVOwner,
    const gfx::TextureViewPtr& viewUV,
    const YuvFrameData& frame,
    gfx::TextureFootprint& outFp,
    const gfx::TimelineFencePtr& waitFence,
    std::uint64_t waitValue,
    Totals& totals) {
    gfx::TextureDesc outDesc{};
    outDesc.width = frame.width;
    outDesc.height = frame.height;
    outDesc.format = gfx::Format::RGBA8UNorm;
    outDesc.storage = true;
    auto outTex = take(device.createTexture(outDesc), "create RGBA8 output texture", totals);
    outFp = outTex->footprint();

    auto cbUp = take(device.createBuffer({256, gfx::Memory::Upload}), "create VideoParams CB upload", totals);
    auto cbBuf = take(device.createBuffer({256, gfx::Memory::Device}), "create VideoParams CB device", totals);
    std::array<std::byte, 256> cbData{};
    std::memcpy(cbData.data(), &frame.params, sizeof(frame.params));
    take(cbUp->write(0, cbData), "write VideoParams CB", totals);

    auto readback = take(device.createBuffer({outFp.size, gfx::Memory::Readback}), "create RGB readback buffer", totals);

    auto cmd = take(device.createCommandList(), "create convert cmd", totals);
    take(cmd->begin(), "begin convert cmd", totals);
    take(cmd->transition(cbBuf, gfx::ResourceState::CopyDestination), "transition CB to CopyDst", totals);
    take(cmd->copyBuffer(cbUp, cbBuf, 256), "copy CB to device", totals);
    take(cmd->transition(cbBuf, gfx::ResourceState::ConstantBuffer), "transition CB to ConstantBuffer", totals);
    take(cmd->transition(texYOwner, gfx::ResourceState::ShaderRead), "transition Y to ShaderRead", totals);
    if (texUVOwner != texYOwner) {
        take(cmd->transition(texUVOwner, gfx::ResourceState::ShaderRead), "transition UV to ShaderRead", totals);
    }
    take(cmd->transition(outTex, gfx::ResourceState::UnorderedAccess), "transition outRgb to UAV", totals);
    take(cmd->setComputePipeline(pipeline), "set YUV->RGB compute pipeline", totals);

    gfx::ResourceBindings bindings{};
    bindings.textures = {
        {0, 0, nullptr, viewY},
        {1, 0, nullptr, viewUV},
    };
    bindings.storageTextures = {
        {2, 0, outTex, nullptr},
    };
    bindings.constants = {
        {3, 0, cbBuf, 0, 256},
    };
    take(cmd->bindComputeResources(bindings), "bind YUV->RGB resources", totals);
    take(cmd->dispatch((frame.width + 7u) / 8u, (frame.height + 7u) / 8u, 1u), "dispatch YUV->RGB", totals);
    take(cmd->transition(outTex, gfx::ResourceState::CopySource), "transition outRgb to CopySource", totals);
    take(cmd->copyTextureToBuffer(outTex, readback), "copy outRgb to readback", totals);
    take(cmd->end(), "end convert cmd", totals);

    if (waitFence) {
        gfx::SubmitDesc sd{};
        gfx::CommandList* listPtr = cmd.get();
        sd.commandLists = std::span<gfx::CommandList* const>(&listPtr, 1);
        gfx::TimelineWait w{waitFence, waitValue};
        sd.waits = std::span<const gfx::TimelineWait>(&w, 1);
        auto fence = take(device.queue().submit(sd), "submit wait+convert", totals);
        take(device.queue().wait(fence, std::chrono::milliseconds(5000)), "wait convert fence", totals);
    } else {
        auto fence = take(device.queue().submit(*cmd), "submit convert", totals);
        take(device.queue().wait(fence, std::chrono::milliseconds(5000)), "wait convert fence", totals);
    }
    return take(readback->read(0, outFp.size), "readback RGB frame", totals);
}

void testCpuPlanarContracts(Totals& totals) {
    check(gfx::isMultiPlanarFormat(gfx::Format::NV12), "NV12 must be multi-planar", totals);
    check(gfx::isMultiPlanarFormat(gfx::Format::P010), "P010 must be multi-planar", totals);
    check(!gfx::isMultiPlanarFormat(gfx::Format::RGBA8UNorm), "RGBA8 must not be multi-planar", totals);
    check(gfx::formatPlaneCount(gfx::Format::NV12) == 2u, "NV12 plane count == 2", totals);
    check(gfx::formatPlaneCount(gfx::Format::P010) == 2u, "P010 plane count == 2", totals);
    check(gfx::formatPlaneCount(gfx::Format::R8UNorm) == 1u, "R8UNorm plane count == 1", totals);

    check(take(gfx::formatPlaneFormat(gfx::Format::NV12, 0), "NV12 plane 0", totals) == gfx::Format::R8UNorm, "NV12 P0 == R8", totals);
    check(take(gfx::formatPlaneFormat(gfx::Format::NV12, 1), "NV12 plane 1", totals) == gfx::Format::RG8UNorm, "NV12 P1 == RG8", totals);
    check(take(gfx::formatPlaneFormat(gfx::Format::P010, 0), "P010 plane 0", totals) == gfx::Format::R16UNorm, "P010 P0 == R16", totals);
    check(take(gfx::formatPlaneFormat(gfx::Format::P010, 1), "P010 plane 1", totals) == gfx::Format::RG16UNorm, "P010 P1 == RG16", totals);
    expectError(gfx::formatPlaneFormat(gfx::Format::NV12, 2), gfx::ErrorCode::InvalidArgument, "NV12 plane 2 rejected", totals);
    expectError(gfx::formatPlaneFormat(gfx::Format::RGBA8UNorm, 1), gfx::ErrorCode::InvalidArgument, "RGBA8 plane 1 rejected", totals);

    // Reject odd dimensions, renderTarget, storage, mipLevels > 1, MSAA.
    expectError(gfx::validateTextureDescription({127, 128, gfx::Format::NV12}), gfx::ErrorCode::InvalidArgument, "odd width NV12", totals);
    expectError(gfx::validateTextureDescription({128, 127, gfx::Format::P010}), gfx::ErrorCode::InvalidArgument, "odd height P010", totals);
    {
        gfx::TextureDesc d{128, 128, gfx::Format::NV12}; d.renderTarget = true;
        expectError(gfx::validateTextureDescription(d), gfx::ErrorCode::Unsupported, "NV12 renderTarget", totals);
    }
    {
        gfx::TextureDesc d{128, 128, gfx::Format::NV12}; d.storage = true;
        expectError(gfx::validateTextureDescription(d), gfx::ErrorCode::Unsupported, "NV12 storage", totals);
    }
    {
        gfx::TextureDesc d{128, 128, gfx::Format::NV12}; d.mipLevels = 2;
        expectError(gfx::validateTextureDescription(d), gfx::ErrorCode::InvalidArgument, "NV12 mipLevels > 1", totals);
    }
    {
        gfx::TextureDesc d{128, 128, gfx::Format::NV12}; d.sampleCount = 4;
        expectError(gfx::validateTextureDescription(d), gfx::ErrorCode::InvalidArgument, "NV12 MSAA", totals);
    }
    // View validation
    gfx::TextureDesc nv12Desc{128, 128, gfx::Format::NV12};
    {
        gfx::TextureViewDesc vd{}; vd.planeSlice = 0;
        auto resolved = take(gfx::validateTextureView(nv12Desc, vd), "NV12 plane 0 view", totals);
        check(resolved.format == gfx::Format::R8UNorm, "NV12 plane 0 inferred R8UNorm", totals);
    }
    {
        gfx::TextureViewDesc vd{}; vd.planeSlice = 1;
        auto resolved = take(gfx::validateTextureView(nv12Desc, vd), "NV12 plane 1 view", totals);
        check(resolved.format == gfx::Format::RG8UNorm, "NV12 plane 1 inferred RG8UNorm", totals);
    }
    {
        gfx::TextureViewDesc vd{}; vd.planeSlice = 1; vd.format = gfx::Format::R8UNorm;
        expectError(gfx::validateTextureView(nv12Desc, vd), gfx::ErrorCode::InvalidArgument, "NV12 plane 1 wrong format", totals);
    }
    {
        gfx::TextureViewDesc vd{}; vd.planeSlice = 2;
        expectError(gfx::validateTextureView(nv12Desc, vd), gfx::ErrorCode::InvalidArgument, "NV12 plane 2 view", totals);
    }
}

void runSafeFallbackPath(
    gfx::Device& device,
    const gfx::PipelinePtr& pipeline,
    bool tenBit,
    const std::string& tag,
    Totals& totals) {
    constexpr std::uint32_t kW = 128, kH = 128;
    const auto frame = buildDeterministicYuvFrame(kW, kH, tenBit, tenBit ? 5u : 3u, totals);

    auto texY = take(device.createTexture({kW, kH, tenBit ? gfx::Format::R16UNorm : gfx::Format::R8UNorm}), "fallback Y tex", totals);
    auto texUV = take(device.createTexture({kW / 2u, kH / 2u, tenBit ? gfx::Format::RG16UNorm : gfx::Format::RG8UNorm}), "fallback UV tex", totals);
    auto viewY = take(device.createTextureView(texY, {}), "fallback Y view", totals);
    auto viewUV = take(device.createTextureView(texUV, {}), "fallback UV view", totals);

    auto upY = take(device.createBuffer({frame.yFp.size, gfx::Memory::Upload}), "fallback Y upload", totals);
    auto upUV = take(device.createBuffer({frame.uvFp.size, gfx::Memory::Upload}), "fallback UV upload", totals);
    take(upY->write(0, frame.yPlane), "write fallback Y", totals);
    take(upUV->write(0, frame.uvPlane), "write fallback UV", totals);

    auto cmd = take(device.createCommandList(), "fallback upload cmd", totals);
    take(cmd->begin(), "begin fallback upload", totals);
    take(cmd->transition(texY, gfx::ResourceState::CopyDestination), "Y -> CopyDst", totals);
    take(cmd->transition(texUV, gfx::ResourceState::CopyDestination), "UV -> CopyDst", totals);
    take(cmd->copyBufferToTexture(upY, texY), "upload fallback Y", totals);
    take(cmd->copyBufferToTexture(upUV, texUV), "upload fallback UV", totals);
    take(cmd->end(), "end fallback upload", totals);
    auto f = take(device.queue().submit(*cmd), "submit fallback upload", totals);
    take(device.queue().wait(f, std::chrono::milliseconds(5000)), "wait fallback upload", totals);

    gfx::TextureFootprint rgbFp{};
    const auto rgb = runYuvConvertCompute(device, pipeline, texY, viewY, texUV, viewUV, frame, rgbFp, nullptr, 0, totals);
    verifyRgbAgainstCpuBt709(frame, rgb, rgbFp, tag + (tenBit ? "/P010-Fallback" : "/NV12-Fallback"), totals);
    std::cout << "  [" << tag << "] " << (tenBit ? "P010" : "NV12") << " Safe-Copy Fallback 128x128 BT.709: PASS\n";
}

void runMultiPlanarAndZeroCopySuite(
    gfx::Backend backend,
    const gfx::AdapterInfo& adapter,
    bool validation,
    bool warp,
    Totals& totals) {
    const std::string tag = std::string(backend == gfx::Backend::D3D12 ? "D3D12/" : "Vulkan/") + adapter.name;
    std::cout << "=== Zero-Copy Video " << tag << (validation ? " [validation]" : "") << " ===\n";

    gfx::DeviceDesc desc{};
    desc.backend = backend;
    desc.adapter = adapter.id;
    desc.softwareAdapter = warp;
    desc.validation = validation;

    auto dev = take(gfx::createDevice(desc), "create video device", totals);
    auto pipeline = makeYuvToRgbPipeline(*dev, backend, totals);

    const auto nv12Sup = take(dev->queryFormatSupport(gfx::Format::NV12), "queryFormatSupport NV12", totals);
    const auto p010Sup = take(dev->queryFormatSupport(gfx::Format::P010), "queryFormatSupport P010", totals);
    std::cout << "  [" << tag << "] format support: NV12=" << (nv12Sup.texture2D ? "yes" : "no")
              << " P010=" << (p010Sup.texture2D ? "yes" : "no")
              << " interop=" << (dev->caps().externalMemoryInterop && dev->caps().externalFenceInterop ? "yes" : "no") << "\n";

    // Always verify Safe-Copy Fallback for both NV12 and P010.
    runSafeFallbackPath(*dev, pipeline, false, tag, totals);
    runSafeFallbackPath(*dev, pipeline, true, tag, totals);

    // Test multi-planar Direct + Zero-Copy for each supported format (NV12, P010).
    for (const bool tenBit : {false, true}) {
        const gfx::Format fmt = tenBit ? gfx::Format::P010 : gfx::Format::NV12;
        const std::string fmtName = tenBit ? "P010" : "NV12";
        const auto& sup = tenBit ? p010Sup : nv12Sup;
        constexpr std::uint32_t kW = 128, kH = 128;

        if (!sup.texture2D) {
            expectError(dev->createTexture({kW, kH, fmt}), gfx::ErrorCode::Unsupported,
                "unsupported multi-planar createTexture " + fmtName, totals);
            std::cout << "  [" << tag << "] " << fmtName << " native multi-planar unsupported -> rejected & fallback verified\n";
            continue;
        }

        // 1. Direct multi-planar upload + per-plane views + BT.709 compute conversion.
        const auto frame = buildDeterministicYuvFrame(kW, kH, tenBit, tenBit ? 9u : 7u, totals);
        auto yuvTex = take(dev->createTexture({kW, kH, fmt}), "create " + fmtName + " texture", totals);
        auto viewY = take(dev->createTextureView(yuvTex, {std::nullopt, gfx::TextureViewUsage::ShaderRead, 0, 1, 0, 1, std::nullopt, 0}), "create Y view", totals);
        auto viewUV = take(dev->createTextureView(yuvTex, {std::nullopt, gfx::TextureViewUsage::ShaderRead, 0, 1, 0, 1, std::nullopt, 1}), "create UV view", totals);

        auto upY = take(dev->createBuffer({frame.yFp.size, gfx::Memory::Upload}), "create Y upload buf", totals);
        auto upUV = take(dev->createBuffer({frame.uvFp.size, gfx::Memory::Upload}), "create UV upload buf", totals);
        take(upY->write(0, frame.yPlane), "write Y plane", totals);
        take(upUV->write(0, frame.uvPlane), "write UV plane", totals);

        auto cmd = take(dev->createCommandList(), "create planar upload cmd", totals);
        take(cmd->begin(), "begin planar upload", totals);
        take(cmd->transition(yuvTex, gfx::ResourceState::CopyDestination), "YUV -> CopyDst", totals);
        // Single-plane copyBufferToTexture must be rejected for multi-planar textures.
        expectError(cmd->copyBufferToTexture(upY, yuvTex), gfx::ErrorCode::InvalidArgument, "copyBufferToTexture on multi-planar", totals);
        // Plane 2 copy must be rejected.
        expectError(cmd->copyBufferToTextureRegion(upY, yuvTex, {0, frame.yFp.rowPitch, 0, 0, kW, kH, 0, 0, 0, 1, 2}),
            gfx::ErrorCode::InvalidArgument, "copyBufferToTextureRegion planeSlice=2", totals);

        take(cmd->copyBufferToTextureRegion(upY, yuvTex, {0, frame.yFp.rowPitch, 0, 0, kW, kH, 0, 0, 0, 1, 0}), "upload Y plane", totals);
        take(cmd->copyBufferToTextureRegion(upUV, yuvTex, {0, frame.uvFp.rowPitch, 0, 0, kW / 2u, kH / 2u, 0, 0, 0, 1, 1}), "upload UV plane", totals);
        take(cmd->end(), "end planar upload", totals);
        auto f = take(dev->queue().submit(*cmd), "submit planar upload", totals);
        take(dev->queue().wait(f, std::chrono::milliseconds(5000)), "wait planar upload", totals);

        gfx::TextureFootprint rgbFp{};
        const auto rgb = runYuvConvertCompute(*dev, pipeline, yuvTex, viewY, yuvTex, viewUV, frame, rgbFp, nullptr, 0, totals);
        verifyRgbAgainstCpuBt709(frame, rgb, rgbFp, tag + "/" + fmtName + "-Direct", totals);
        std::cout << "  [" << tag << "] " << fmtName << " Direct Multi-Planar 128x128 BT.709: PASS\n";

        // 2. Zero-Copy Shared Hardware Decode -> Render Device path (on physical GPUs with interop).
        if (!warp && dev->caps().externalMemoryInterop && dev->caps().externalFenceInterop) {
            auto decoderDev = take(gfx::createDevice(desc), "create decoder device", totals);
            const auto zcFrame = buildDeterministicYuvFrame(kW, kH, tenBit, tenBit ? 15u : 13u, totals);

            gfx::TextureDesc sharedDesc{kW, kH, fmt};
            sharedDesc.shared = true;
            auto sharedYuvRes = decoderDev->createTexture(sharedDesc);
            if (!sharedYuvRes && sharedYuvRes.error().code == gfx::ErrorCode::Unsupported) {
                std::cout << "  [" << tag << "] " << fmtName << " shared multi-planar unsupported -> fallback already verified\n";
            } else {
                auto sharedYuvDec = take(std::move(sharedYuvRes), "create shared decoder " + fmtName, totals);
                auto sharedFenceDec = take(decoderDev->createSharedTimelineFence(0), "create shared decoder fence", totals);

                auto decUpY = take(decoderDev->createBuffer({zcFrame.yFp.size, gfx::Memory::Upload}), "dec Y upload", totals);
                auto decUpUV = take(decoderDev->createBuffer({zcFrame.uvFp.size, gfx::Memory::Upload}), "dec UV upload", totals);
                take(decUpY->write(0, zcFrame.yPlane), "write dec Y", totals);
                take(decUpUV->write(0, zcFrame.uvPlane), "write dec UV", totals);

                auto texHandle = take(decoderDev->exportTextureHandle(sharedYuvDec), "export shared " + fmtName, totals);
                auto fenceHandle = take(decoderDev->exportTimelineFenceHandle(sharedFenceDec), "export shared fence", totals);

                auto importedYuv = take(dev->importTextureHandle(sharedDesc, texHandle), "import shared " + fmtName, totals);
                auto importedFence = take(dev->importTimelineFenceHandle(fenceHandle), "import shared fence", totals);
                auto impViewY = take(dev->createTextureView(importedYuv, {std::nullopt, gfx::TextureViewUsage::ShaderRead, 0, 1, 0, 1, std::nullopt, 0}), "import Y view", totals);
                auto impViewUV = take(dev->createTextureView(importedYuv, {std::nullopt, gfx::TextureViewUsage::ShaderRead, 0, 1, 0, 1, std::nullopt, 1}), "import UV view", totals);

                // Decoder uploads Y & UV planes and signals timeline fence value 1.
                auto decCmd = take(decoderDev->createCommandList(), "decoder cmd", totals);
                take(decCmd->begin(), "begin decoder cmd", totals);
                take(decCmd->transition(sharedYuvDec, gfx::ResourceState::CopyDestination), "dec YUV -> CopyDst", totals);
                take(decCmd->copyBufferToTextureRegion(decUpY, sharedYuvDec, {0, zcFrame.yFp.rowPitch, 0, 0, kW, kH, 0, 0, 0, 1, 0}), "dec upload Y", totals);
                take(decCmd->copyBufferToTextureRegion(decUpUV, sharedYuvDec, {0, zcFrame.uvFp.rowPitch, 0, 0, kW / 2u, kH / 2u, 0, 0, 0, 1, 1}), "dec upload UV", totals);
                take(decCmd->transition(sharedYuvDec, gfx::ResourceState::Common), "dec YUV -> Common", totals);
                take(decCmd->end(), "end decoder cmd", totals);

                gfx::SubmitDesc decSd{};
                gfx::CommandList* decPtr = decCmd.get();
                decSd.commandLists = std::span<gfx::CommandList* const>(&decPtr, 1);
                gfx::TimelineSignal sig{sharedFenceDec, 1};
                decSd.signals = std::span<const gfx::TimelineSignal>(&sig, 1);
                auto decFence = take(decoderDev->queue().submit(decSd), "submit decoder frame", totals);

                // Render device waits on GPU timeline fence 1 and runs Zero-Copy BT.709 compute!
                gfx::TextureFootprint zcFp{};
                const auto zcRgb = runYuvConvertCompute(*dev, pipeline, importedYuv, impViewY, importedYuv, impViewUV, zcFrame, zcFp, importedFence, 1, totals);
                verifyRgbAgainstCpuBt709(zcFrame, zcRgb, zcFp, tag + "/" + fmtName + "-ZeroCopy", totals);
                take(decoderDev->queue().wait(decFence, std::chrono::milliseconds(5000)), "wait decoder fence", totals);

                if (validation) {
                    const auto decVal = take(decoderDev->validationErrorCount(), "decoder validationErrorCount", totals);
                    check(decVal == 0, "decoder validationErrorCount must be 0", totals);
                }
                std::cout << "  [" << tag << "] " << fmtName << " Zero-Copy Shared Interop 128x128 BT.709: PASS\n";
            }
        }
    }

    if (validation) {
        const auto val = take(dev->validationErrorCount(), "validationErrorCount", totals);
        check(val == 0, "validationErrorCount must be 0", totals);
        std::cout << "  [" << tag << "] validationErrorCount=0\n";
    }
    ++totals.adapters;
}

} // namespace

int main(int argc, char** argv) {
    bool useD3D12 = false;
    bool validation = false;
    bool warp = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--d3d12") useD3D12 = true;
        else if (arg == "--vulkan") useD3D12 = false;
        else if (arg == "--validation") validation = true;
        else if (arg == "--warp") { warp = true; useD3D12 = true; }
    }

    try {
        Totals totals{};
        testCpuPlanarContracts(totals);
        const gfx::Backend backend = useD3D12 ? gfx::Backend::D3D12 : gfx::Backend::Vulkan;
        auto adapters = take(gfx::enumerateAdapters(backend), "enumerateAdapters", totals);
        for (const auto& a : adapters) {
            if (warp) {
                if (!a.softwareAdapter) continue;
            } else {
                if (a.softwareAdapter) continue;
            }
            runMultiPlanarAndZeroCopySuite(backend, a, validation, warp, totals);
        }
        check(totals.adapters > 0, "Expected at least one matching adapter", totals);
        std::cout << "PASS gfx_zero_copy_video_tests: adapters=" << totals.adapters
                  << " frames=" << totals.framesVerified
                  << " assertions=" << totals.assertions
                  << " rejections=" << totals.rejections << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
