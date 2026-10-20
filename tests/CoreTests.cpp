#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace {
int assertions{};
void checkpoint(const char* text) {
    std::cout << text << std::endl;
#ifdef _WIN32
    OutputDebugStringA(text); OutputDebugStringA("\n");
#endif
}
void check(bool value, const char* message) { ++assertions; if (!value) throw std::runtime_error(message); }
template<class T> T take(gfx::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message + " native=" + std::to_string(result.error().nativeCode));
    return std::move(*result);
}
void take(gfx::Result<void> result) { if (!result) throw std::runtime_error(result.error().message + " native=" + std::to_string(result.error().nativeCode)); }
constexpr std::string_view shader = R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 positions[3] = {float2(-.8,-.8), float2(0,.8), float2(.8,-.8)};
    return float4(positions[id % 3],.5,1);
}
float4 PS() : SV_Target0 { return float4(1,0,0,1); }
)";
void cpu() {
    check(!gfx::backendCompiled(gfx::Backend::Vulkan), "Vulkan must not be advertised");
    check(!gfx::backendCompiled(gfx::Backend::Metal), "Metal must not be advertised");
    auto unsupported = gfx::createDevice({gfx::Backend::Metal});
    check(!unsupported && unsupported.error().code == gfx::ErrorCode::Unsupported, "Unsupported backend error");
    check(!gfx::compileHlsl("", "VS", gfx::ShaderStage::Vertex), "Reject empty HLSL");
    check(!gfx::reflectShader({}), "Reject empty shader binary");
}
void gpu(gfx::Device& device, bool validation) {
    checkpoint("Starting GPU contracts");
    std::cout << "Adapter: " << device.caps().adapterName << " software=" << device.caps().softwareAdapter << '\n';
    check(!device.caps().compute && device.caps().timestampQueries, "Capabilities reflect implementation");
    check(!device.createBuffer({0}), "Reject empty buffers");
    check(!device.createTexture({0, 64}), "Reject empty textures");
    check(!device.createGraphicsPipeline({}), "Reject empty shader pipeline");
    check(!device.queue().wait({UINT64_MAX, 0}), "Reject foreign fence");
    auto upload = take(device.createBuffer({257, gfx::Memory::Upload}));
    std::array<std::byte, 257> bytes{};
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::byte>(i % 251);
    take(upload->write(0, bytes));
    check(!upload->write(256, bytes), "Reject oversized write");
    auto readback = take(device.createBuffer({257, gfx::Memory::Readback}));
    check(!readback->write(0, bytes), "Reject readback write");
    auto commands = take(device.createCommandList());
    check(!commands->end(), "Reject end before begin");
    check(!commands->draw(3), "Reject draw before begin");
    take(commands->begin());
    check(!commands->begin(), "Reject nested recording");
    take(commands->copyBuffer(upload, readback, bytes.size())); take(commands->end());
    take(upload->write(0, bytes));
    check(!device.queue().submit(*commands), "Reject upload mutated after recording");
    take(commands->begin()); take(commands->copyBuffer(upload, readback, bytes.size())); take(commands->end());
    auto fence = take(device.queue().submit(*commands));
    check(!device.queue().submit(*commands), "Reject duplicate submit");
    // Destroy the command list and upload wrapper before GPU completion.
    commands.reset(); upload.reset();
    take(device.queue().wait(fence));
    check(take(readback->read(0, bytes.size())) == std::vector<std::byte>(bytes.begin(), bytes.end()), "Buffer copy readback matches");
    checkpoint("Buffer lifetime/readback passed");
    auto deviceBuffer=take(device.createBuffer({bytes.size()}));
    upload=take(device.createBuffer({bytes.size(),gfx::Memory::Upload})); take(upload->write(0,bytes));
    for (unsigned repeat=0;repeat<2;++repeat) {
        commands=take(device.createCommandList()); take(commands->begin());
        take(commands->transition(deviceBuffer,gfx::ResourceState::CopyDestination)); take(commands->copyBuffer(upload,deviceBuffer,bytes.size()));
        take(commands->transition(deviceBuffer,gfx::ResourceState::CopySource)); take(commands->copyBuffer(deviceBuffer,readback,bytes.size()));
        take(commands->end()); fence=take(device.queue().submit(*commands)); take(device.queue().wait(fence));
        check(take(readback->read(0,bytes.size()))==std::vector<std::byte>(bytes.begin(),bytes.end()),"Default-heap buffer state across Execute batches");
    }
    checkpoint("Default buffer batch state passed");

    // Odd width exercises 256-byte row pitch and the final unpadded row footprint.
    auto texture = take(device.createTexture({63, 7}));
    const auto layout = texture->footprint();
    check(layout.rowBytes == 252 && layout.rowPitch % 256 == 0 && layout.size >= layout.rowBytes + 6ull*layout.rowPitch, "Texture footprint alignment");
    auto staging = take(device.createBuffer({layout.size, gfx::Memory::Upload}));
    std::vector<std::byte> pixels(static_cast<size_t>(layout.size), std::byte{0});
    for (std::uint32_t y = 0; y < 7; ++y) for (std::uint32_t x = 0; x < layout.rowBytes; ++x) pixels[y*layout.rowPitch+x] = static_cast<std::byte>((x+13*y) % 251);
    take(staging->write(0, pixels));
    auto download = take(device.createBuffer({layout.size, gfx::Memory::Readback}));
    commands = take(device.createCommandList()); take(commands->begin());
    take(commands->transition(texture, gfx::ResourceState::CopyDestination));
    take(commands->copyBufferToTexture(staging, texture));
    take(commands->transition(texture, gfx::ResourceState::CopySource));
    take(commands->copyTextureToBuffer(texture, download)); take(commands->end());
    fence = take(device.queue().submit(*commands)); commands.reset(); staging.reset(); texture.reset();
    take(device.queue().wait(fence));
    auto restored = take(download->read(0, layout.size));
    for (std::uint32_t y = 0; y < 7; ++y) check(std::equal(pixels.begin()+y*layout.rowPitch, pixels.begin()+y*layout.rowPitch+layout.rowBytes, restored.begin()+y*layout.rowPitch), "Texture upload/download row matches");
    checkpoint("Texture lifetime/readback passed");

    auto target = take(device.createTexture({64, 64, gfx::Format::RGBA8UNorm, true}));
    auto vertex = take(gfx::compileHlsl(shader, "VS", gfx::ShaderStage::Vertex));
    auto fragment = take(gfx::compileHlsl(shader, "PS", gfx::ShaderStage::Fragment));
    check(take(gfx::reflectShader(vertex)).empty(), "Vertex-ID shader has no resource bindings");
    check(!gfx::compileHlsl("invalid hlsl", "PS", gfx::ShaderStage::Fragment), "Compile diagnostics propagate");
    auto pipeline = take(device.createGraphicsPipeline({vertex, fragment}));
    auto targetLayout = target->footprint();
    auto image = take(device.createBuffer({targetLayout.size, gfx::Memory::Readback}));
    commands = take(device.createCommandList()); take(commands->begin());
    take(commands->writeTimestamp(0));
    check(!commands->writeTimestamp(0), "Reject duplicate timestamp");
    take(commands->transition(target, gfx::ResourceState::RenderTarget));
    take(commands->beginRenderPass(target, {0, 0, 1, 1}));
    check(!commands->end(), "Reject ending with open pass");
    check(!commands->draw(3), "Reject drawing without a pipeline");
    take(commands->setPipeline(pipeline)); take(commands->setScissor(0, 0, 64, 64));
    check(!commands->setScissor(UINT32_MAX, 0, 64, 64), "Reject overflowing scissor");
    take(commands->draw(3)); take(commands->endRenderPass());
    take(commands->writeTimestamp(1)); take(commands->resolveTimestamps());
    take(commands->transition(target, gfx::ResourceState::CopySource)); take(commands->copyTextureToBuffer(target, image)); take(commands->end());
    fence = take(device.queue().submit(*commands));
    pipeline.reset(); target.reset(); take(device.queue().wait(fence));
    double gpuTime = take(commands->elapsedGpuMilliseconds()); check(gpuTime >= 0, "GPU timestamp result");
    auto rgba = take(image->read(0, targetLayout.size));
    auto component = [&](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return std::to_integer<unsigned>(rgba[y*targetLayout.rowPitch+x*4+c]); };
    check(component(32, 32, 0) == 255 && component(32, 32, 2) == 0, "Triangle center red");
    check(component(0, 0, 0) == 0 && component(0, 0, 2) == 255, "Clear corner blue");
    std::cout << "Triangle GPU timestamps: " << gpuTime << " ms (correctness run, not performance baseline)\n";
    checkpoint("Triangle readback passed");

    constexpr std::string_view sampledSource=R"(
Texture2D image : register(t0); SamplerState sampling : register(s0);
float4 VS(uint id:SV_VertexID):SV_Position {
    float2 p[3]={float2(-.8,-.8),float2(0,.8),float2(.8,-.8)};
    return float4(p[id%3],.5,1);
}
float4 PS():SV_Target0 { return image.Sample(sampling,float2(.5,.5)); }
)";
    auto sampledFragment=take(gfx::compileHlsl(sampledSource,"PS",gfx::ShaderStage::Fragment));
    check(take(gfx::reflectShader(sampledFragment)).size()==2,"Reflect texture and sampler bindings");
    auto sampledPipeline=take(device.createGraphicsPipeline({vertex,sampledFragment,gfx::Format::RGBA8UNorm,true}));
    auto sampledTexture=take(device.createTexture({4,4})); auto sampledLayout=sampledTexture->footprint();
    auto sampledUpload=take(device.createBuffer({sampledLayout.size,gfx::Memory::Upload}));
    std::vector<std::byte> green(static_cast<size_t>(sampledLayout.size));
    for (std::uint32_t y=0;y<4;++y) for (std::uint32_t x=0;x<4;++x) { green[y*sampledLayout.rowPitch+x*4+1]=std::byte{255}; green[y*sampledLayout.rowPitch+x*4+3]=std::byte{255}; }
    take(sampledUpload->write(0,green)); target=take(device.createTexture({64,64,gfx::Format::RGBA8UNorm,true}));
    commands=take(device.createCommandList()); take(commands->begin());
    take(commands->transition(sampledTexture,gfx::ResourceState::CopyDestination)); take(commands->copyBufferToTexture(sampledUpload,sampledTexture));
    take(commands->transition(sampledTexture,gfx::ResourceState::ShaderRead)); take(commands->transition(target,gfx::ResourceState::RenderTarget));
    take(commands->beginRenderPass(target,{0,0,1,1})); take(commands->setPipeline(sampledPipeline));
    check(!commands->draw(3),"Reject sampled draw without a texture binding");
    for (unsigned i=0;i<256;++i) take(commands->bindTexture(sampledTexture));
    check(!commands->bindTexture(sampledTexture),"Reject descriptor heap exhaustion");
    take(commands->draw(3)); take(commands->endRenderPass()); take(commands->transition(target,gfx::ResourceState::CopySource)); take(commands->copyTextureToBuffer(target,image)); take(commands->end());
    fence=take(device.queue().submit(*commands)); sampledTexture.reset(); sampledUpload.reset(); sampledPipeline.reset(); commands.reset(); target.reset();
    take(device.queue().wait(fence)); rgba=take(image->read(0,targetLayout.size));
    check(component(32,32,1)==255 && component(32,32,0)==0,"Sampled descriptor lifetime and green pixel");
    checkpoint("Sampled binding and descriptor lifetime passed");

    // Two independently recorded state changes may not both commit an outdated snapshot.
    texture = take(device.createTexture({8,8}));
    auto first = take(device.createCommandList()), second = take(device.createCommandList());
    take(first->begin()); take(first->transition(texture, gfx::ResourceState::CopyDestination)); take(first->end());
    take(second->begin()); take(second->transition(texture, gfx::ResourceState::CopySource)); take(second->end());
    fence = take(device.queue().submit(*first)); check(!device.queue().submit(*second), "Reject stale recorded resource state"); take(device.queue().wait(fence));
    checkpoint("Stale command rejection passed");

    auto foreign = take(gfx::createDevice({gfx::Backend::D3D12, false, device.caps().softwareAdapter}));
    auto other = take(foreign->createBuffer({16}));
    commands = take(device.createCommandList()); take(commands->begin());
    auto wrong = commands->transition(other, gfx::ResourceState::CopyDestination);
    check(!wrong && wrong.error().code == gfx::ErrorCode::WrongDevice, "Reject resource from another device");
    take(commands->end());
    bool threadRejected{};
    std::thread worker([&] { auto result = device.createBuffer({16}); threadRejected = !result && result.error().code == gfx::ErrorCode::InvalidState; }); worker.join();
    check(threadRejected, "Reject access from another thread");
    checkpoint("Device and thread rejection passed");

#ifdef _WIN32
    HWND window = CreateWindowExW(0, L"STATIC", L"GraphicsSDK automated swapchain", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(window != nullptr, "Create hidden test window");
    {
        auto swap = take(device.createSwapChain({gfx::SurfaceKind::Win32, window}, {64,64,2,false}));
        for (int frame = 0; frame < 4; ++frame) {
            auto backbuffer = take(swap->acquire());
            auto cmd = take(device.createCommandList()); take(cmd->begin());
            take(cmd->transition(backbuffer, gfx::ResourceState::RenderTarget)); take(cmd->beginRenderPass(backbuffer, {.25f,.5f,.75f,1})); take(cmd->endRenderPass());
            take(cmd->transition(backbuffer, gfx::ResourceState::Present)); take(cmd->end()); fence = take(device.queue().submit(*cmd));
            take(swap->present()); take(device.queue().wait(fence));
        }
        auto held = take(swap->acquire()); check(!swap->resize(96,96), "Resize rejects live backbuffer references"); held.reset();
        auto recorded=take(device.createCommandList()); take(recorded->begin());
        take(recorded->transition(take(swap->acquire()),gfx::ResourceState::RenderTarget)); take(recorded->end());
        check(!swap->resize(96,96),"Resize rejects references in unsubmitted commands"); recorded.reset();
        take(swap->resize(96,96)); check(take(swap->acquire())->description().width == 96, "Swapchain resize extent");
    }
    DestroyWindow(window);
    checkpoint("Swapchain presentation/resize passed");
#endif
    take(device.queue().waitIdle());
    if (validation) check(take(device.validationErrorCount()) == 0, "D3D12 validation reports errors");
}
}
int main(int argc, char** argv) try {
    std::string_view mode = argc > 1 ? argv[1] : "--cpu";
    if (mode == "--cpu") cpu();
    else {
        bool validation = mode == "--validation";
        auto device = gfx::createDevice({gfx::Backend::D3D12, validation, mode == "--warp"});
        if (!device && device.error().code == gfx::ErrorCode::Unsupported) { std::cout << "SKIP: " << device.error().message << '\n'; return 77; }
        gpu(*take(std::move(device)), validation);
    }
    std::cout << "PASS: " << assertions << " assertions\n"; return 0;
} catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
