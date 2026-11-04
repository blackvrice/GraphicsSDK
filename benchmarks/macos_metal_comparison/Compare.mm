#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#if defined(COMPARE_sdk)
#include <graphics/Graphics.hpp>
#elif defined(COMPARE_sdl)
#include <SDL3/SDL.h>
#elif defined(COMPARE_llgl)
#include <LLGL/LLGL.h>
#include <LLGL/Backend/Metal/NativeHandle.h>
#endif

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr unsigned width = 64, height = 32, pitch = 4 * width, imageBytes = pitch * height;
constexpr std::array draws{1u, 128u, 1024u};
constexpr unsigned warmups = 4, samples = 12;
using Pixels = std::array<unsigned char, imageBytes>;
using Clock = std::chrono::steady_clock;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Intervals {
    std::int64_t record{}, submit{}, wait{};
};

std::int64_t nanos(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
}

struct Host {
    id<MTLDevice> device;
    std::uint64_t registry;
    const char* name;
    bool mac1, mac2;

    Host() {
        auto devices = [MTLCopyAllDevices() autorelease];
        device = [MTLCreateSystemDefaultDevice() autorelease];
        require(devices.count == 1 && device && device.registryID && devices[0].registryID == device.registryID,
                "Comparison requires exactly one enumerated Metal adapter and the same system default");
        registry = device.registryID;
        name = device.name.UTF8String;
        require(name && *name, "Metal adapter name unavailable");
        mac1 = [device supportsFamily:MTLGPUFamilyMac1];
        mac2 = [device supportsFamily:MTLGPUFamilyMac2];
    }
};

#if defined(COMPARE_sdk)
constexpr auto adapter = "graphicssdk";
constexpr auto identity = "explicit-registry-id";

template<class T> T take(gfx::Result<T> value) {
    if (!value) throw std::runtime_error(value.error().message);
    return std::move(*value);
}
void take(gfx::Result<void> value) {
    if (!value) throw std::runtime_error(value.error().message);
}

struct Renderer {
    std::unique_ptr<gfx::Device> device;
    std::unique_ptr<gfx::CommandList> list;
    gfx::TexturePtr target;
    gfx::PipelinePtr pipeline;
    const char* storage = "private";

    Renderer(const Host& host, const std::string& source) {
        gfx::DeviceDesc desc;
        desc.backend = gfx::Backend::Metal;
        desc.adapter = gfx::AdapterId{gfx::Backend::Metal, host.registry};
        device = take(gfx::createDevice(desc));
        require(device->caps().adapterId == *desc.adapter && device->caps().adapterName == host.name,
                "GraphicsSDK adapter differs");
        list = take(device->createCommandList());
        target = take(device->createTexture({width, height, gfx::Format::RGBA8UNorm, true}));
        gfx::GraphicsPipelineDesc p;
        p.vertex.format = p.fragment.format = gfx::ShaderFormat::MSL;
        p.vertex.stage = gfx::ShaderStage::Vertex;
        p.fragment.stage = gfx::ShaderStage::Fragment;
        p.vertex.bytes.resize(source.size());
        std::memcpy(p.vertex.bytes.data(), source.data(), source.size());
        p.fragment.bytes = p.vertex.bytes;
        pipeline = take(device->createGraphicsPipeline(p));
    }

    Intervals frame(unsigned count, unsigned x) {
        const auto start = Clock::now();
        take(list->begin());
        take(list->transition(target, gfx::ResourceState::RenderTarget));
        take(list->beginRenderPass(target, {0, 0, 1, 1}));
        take(list->setPipeline(pipeline));
        take(list->setScissor(x, 3, 19, 11));
        for (unsigned n = 0; n < count; ++n) take(list->draw(3));
        take(list->endRenderPass());
        take(list->end());
        const auto recorded = Clock::now();
        take(device->queue().submit(*list));
        const auto submitted = Clock::now();
        take(device->queue().waitIdle());
        return {nanos(start, recorded), nanos(recorded, submitted), nanos(submitted, Clock::now())};
    }

    Pixels read() {
        auto buffer = take(device->createBuffer({imageBytes, gfx::Memory::Readback}));
        take(list->begin());
        take(list->transition(target, gfx::ResourceState::CopySource));
        take(list->copyTextureToBuffer(target, buffer));
        take(list->end());
        take(device->queue().submit(*list));
        take(device->queue().waitIdle());
        const auto bytes = take(buffer->read(0, imageBytes));
        Pixels result;
        std::memcpy(result.data(), bytes.data(), result.size());
        return result;
    }
};
#elif defined(COMPARE_sdl)
constexpr auto adapter = "sdl3";
constexpr auto identity = "pinned-default-selection-with-single-adapter";

void sdl(bool valid) {
    if (!valid) throw std::runtime_error(SDL_GetError());
}

struct Renderer {
    SDL_GPUDevice* device{};
    SDL_GPUTexture* target{};
    SDL_GPUGraphicsPipeline* pipeline{};
    const char* storage = "private";

    Renderer(const Host& host, const std::string& source) {
        // The pinned Metal GPU backend requires a video driver with Metal_CreateView.
        sdl(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "cocoa"));
        sdl(SDL_Init(SDL_INIT_VIDEO));
        const auto properties = SDL_CreateProperties();
        sdl(properties != 0);
        sdl(SDL_SetStringProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "metal"));
        sdl(SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true));
        sdl(SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, false));
        sdl(SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN, false));
        sdl(SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_METAL_ALLOW_MACFAMILY1_BOOLEAN,
                                  !host.mac2 && host.mac1));
        device = SDL_CreateGPUDeviceWithProperties(properties);
        const std::string error = device ? "" : SDL_GetError();
        SDL_DestroyProperties(properties);
        if (!device) throw std::runtime_error(error + " (native Mac1=" + std::to_string(host.mac1) +
                                              ", Mac2=" + std::to_string(host.mac2) + ")");
        require(std::string(SDL_GetGPUDeviceDriver(device)) == "metal", "SDL selected a non-Metal driver");
        const char* name = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device), SDL_PROP_GPU_DEVICE_NAME_STRING, "");
        require(std::string(name) == host.name, "SDL selected a different adapter name");
        SDL_GPUTextureCreateInfo t{};
        t.type = SDL_GPU_TEXTURETYPE_2D;
        t.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        t.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        t.width = width;
        t.height = height;
        t.layer_count_or_depth = t.num_levels = 1;
        target = SDL_CreateGPUTexture(device, &t);
        sdl(target != nullptr);
        SDL_GPUShaderCreateInfo shader{};
        shader.code = reinterpret_cast<const Uint8*>(source.c_str());
        shader.code_size = source.size();
        shader.format = SDL_GPU_SHADERFORMAT_MSL;
        shader.entrypoint = "gfx_vertex";
        shader.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        auto* vs = SDL_CreateGPUShader(device, &shader);
        sdl(vs != nullptr);
        shader.entrypoint = "gfx_fragment";
        shader.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        auto* fs = SDL_CreateGPUShader(device, &shader);
        sdl(fs != nullptr);
        SDL_GPUColorTargetDescription color{};
        color.format = t.format;
        SDL_GPUGraphicsPipelineCreateInfo p{};
        p.vertex_shader = vs;
        p.fragment_shader = fs;
        p.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        p.target_info.color_target_descriptions = &color;
        p.target_info.num_color_targets = 1;
        p.rasterizer_state.enable_depth_clip = true;
        pipeline = SDL_CreateGPUGraphicsPipeline(device, &p);
        SDL_ReleaseGPUShader(device, vs);
        SDL_ReleaseGPUShader(device, fs);
        sdl(pipeline != nullptr);
    }

    ~Renderer() {
        SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        SDL_ReleaseGPUTexture(device, target);
        SDL_DestroyGPUDevice(device);
        SDL_Quit();
    }

    Intervals frame(unsigned count, unsigned x) {
        const auto start = Clock::now();
        auto* commands = SDL_AcquireGPUCommandBuffer(device);
        sdl(commands != nullptr);
        SDL_GPUColorTargetInfo color{};
        color.texture = target;
        color.clear_color = {0, 0, 1, 1};
        color.load_op = SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
        sdl(pass != nullptr);
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        SDL_Rect scissor{static_cast<int>(x), 3, 19, 11};
        SDL_SetGPUScissor(pass, &scissor);
        for (unsigned n = 0; n < count; ++n) SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        const auto recorded = Clock::now();
        sdl(SDL_SubmitGPUCommandBuffer(commands));
        const auto submitted = Clock::now();
        sdl(SDL_WaitForGPUIdle(device));
        return {nanos(start, recorded), nanos(recorded, submitted), nanos(submitted, Clock::now())};
    }

    Pixels read() {
        SDL_GPUTransferBufferCreateInfo desc{};
        desc.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        desc.size = imageBytes;
        auto* buffer = SDL_CreateGPUTransferBuffer(device, &desc);
        sdl(buffer != nullptr);
        auto* commands = SDL_AcquireGPUCommandBuffer(device);
        sdl(commands != nullptr);
        auto* copy = SDL_BeginGPUCopyPass(commands);
        sdl(copy != nullptr);
        SDL_GPUTextureRegion region{};
        region.texture = target;
        region.w = width;
        region.h = height;
        region.d = 1;
        SDL_GPUTextureTransferInfo transfer{};
        transfer.transfer_buffer = buffer;
        transfer.pixels_per_row = width;
        transfer.rows_per_layer = height;
        SDL_DownloadFromGPUTexture(copy, &region, &transfer);
        SDL_EndGPUCopyPass(copy);
        sdl(SDL_SubmitGPUCommandBuffer(commands));
        sdl(SDL_WaitForGPUIdle(device));
        auto* mapped = SDL_MapGPUTransferBuffer(device, buffer, false);
        sdl(mapped != nullptr);
        Pixels result;
        std::memcpy(result.data(), mapped, result.size());
        SDL_UnmapGPUTransferBuffer(device, buffer);
        SDL_ReleaseGPUTransferBuffer(device, buffer);
        return result;
    }
};
#elif defined(COMPARE_llgl)
constexpr auto adapter = "llgl";
constexpr auto identity = "injected-and-observed-native-registry-id";

struct Renderer {
    LLGL::RenderSystemPtr device;
    LLGL::CommandQueue* queue{};
    LLGL::CommandBuffer* list{};
    LLGL::Texture* image{};
    LLGL::RenderPass* pass{};
    LLGL::RenderTarget* target{};
    LLGL::PipelineState* pipeline{};
    const char* storage{};

    Renderer(const Host& host, const std::string& source) {
        LLGL::Metal::RenderSystemNativeHandle native{host.device};
        LLGL::RenderSystemDescriptor desc;
        desc.moduleName = "Metal";
        desc.nativeHandle = &native;
        desc.nativeHandleSize = sizeof(native);
        LLGL::Report report;
        device = LLGL::RenderSystem::Load(desc, &report);
        if (!device) throw std::runtime_error(report.GetText());
        LLGL::Metal::RenderSystemNativeHandle observed{};
        require(device->GetNativeHandle(&observed, sizeof(observed)), "LLGL has no Metal device handle");
        const bool matches = observed.device.registryID == host.registry;
        [observed.device release];
        require(matches, "LLGL selected a different adapter");
        queue = device->GetCommandQueue();
        list = device->CreateCommandBuffer({});
        LLGL::TextureDescriptor t;
        t.type = LLGL::TextureType::Texture2D;
        t.format = LLGL::Format::RGBA8UNorm;
        t.bindFlags = LLGL::BindFlags::ColorAttachment | LLGL::BindFlags::CopySrc;
        t.miscFlags = LLGL::MiscFlags::NoInitialData;
        t.extent = {width, height, 1};
        t.mipLevels = 1;
        image = device->CreateTexture(t);
        require(image && queue && list, "LLGL render resources unavailable");
        LLGL::Metal::ResourceNativeHandle texture{};
        require(image->GetNativeHandle(&texture, sizeof(texture)), "LLGL has no Metal texture handle");
        const auto mode = texture.texture.storageMode;
        [texture.texture release];
        storage = mode == MTLStorageModeManaged ? "managed" : mode == MTLStorageModePrivate ? "private" : "shared";

        LLGL::RenderPassDescriptor rp;
        rp.colorAttachments[0] = {t.format, LLGL::AttachmentLoadOp::Clear, LLGL::AttachmentStoreOp::Store};
        pass = device->CreateRenderPass(rp);
        LLGL::RenderTargetDescriptor rt;
        rt.renderPass = pass;
        rt.resolution = {width, height};
        rt.colorAttachments[0].texture = image;
        target = device->CreateRenderTarget(rt);
        LLGL::ShaderDescriptor shader;
        shader.source = source.c_str();
        shader.sourceType = LLGL::ShaderSourceType::CodeString;
        shader.type = LLGL::ShaderType::Vertex;
        shader.entryPoint = "gfx_vertex";
        shader.profile = "2.0";
        auto* vs = device->CreateShader(shader);
        shader.type = LLGL::ShaderType::Fragment;
        shader.entryPoint = "gfx_fragment";
        auto* fs = device->CreateShader(shader);
        require(pass && target && vs && fs, "LLGL shaders or render pass unavailable");
        for (auto* stage : {vs, fs}) {
            if (auto* detail = stage->GetReport(); detail && detail->HasErrors()) throw std::runtime_error(detail->GetText());
        }
        LLGL::GraphicsPipelineDescriptor p;
        p.renderPass = pass;
        p.vertexShader = vs;
        p.fragmentShader = fs;
        p.primitiveTopology = LLGL::PrimitiveTopology::TriangleList;
        p.rasterizer.scissorTestEnabled = true;
        p.viewports.push_back(LLGL::Viewport{0, 0, width, height});
        pipeline = device->CreatePipelineState(p);
        require(pipeline, "LLGL Metal pipeline unavailable");
        if (auto* detail = pipeline->GetReport(); detail && detail->HasErrors()) throw std::runtime_error(detail->GetText());
    }

    Intervals frame(unsigned count, unsigned x) {
        const auto start = Clock::now();
        list->Begin();
        LLGL::ClearValue blue{0, 0, 1, 1};
        list->BeginRenderPass(*target, pass, 1, &blue);
        list->SetPipelineState(*pipeline);
        list->SetScissor({static_cast<int>(x), 3, 19, 11});
        for (unsigned n = 0; n < count; ++n) list->Draw(3, 0);
        list->EndRenderPass();
        list->End();
        const auto recorded = Clock::now();
        queue->Submit(*list);
        const auto submitted = Clock::now();
        queue->WaitIdle();
        return {nanos(start, recorded), nanos(recorded, submitted), nanos(submitted, Clock::now())};
    }

    Pixels read() {
        LLGL::BufferDescriptor desc;
        desc.size = imageBytes;
        desc.bindFlags = LLGL::BindFlags::CopyDst;
        desc.miscFlags = LLGL::MiscFlags::DynamicUsage;
        auto* buffer = device->CreateBuffer(desc);
        require(buffer, "LLGL readback buffer unavailable");
        list->Begin();
        list->CopyBufferFromTexture(*buffer, 0, *image, {{0, 0, 0}, {width, height, 1}}, pitch, imageBytes);
        list->End();
        queue->Submit(*list);
        queue->WaitIdle();
        Pixels result{};
        device->ReadBuffer(*buffer, 0, result.data(), result.size());
        device->Release(*buffer);
        return result;
    }
};
#endif

void verify(const Pixels& image, unsigned x, const std::filesystem::path& path) {
    for (unsigned row = 0; row < height; ++row) {
        for (unsigned col = 0; col < width; ++col) {
            const bool red = col >= x && col < x + 19 && row >= 3 && row < 14;
            const auto p = row * pitch + col * 4;
            if (!(image[p] == (red ? 255 : 0) && image[p+1] == 0 &&
                  image[p+2] == (red ? 0 : 255) && image[p+3] == 255)) {
                std::ofstream raw(path, std::ios::binary);
                raw.write(reinterpret_cast<const char*>(image.data()), image.size());
                throw std::runtime_error("RGBA reference differs at " + std::to_string(col) + "," + std::to_string(row));
            }
        }
    }
    std::ofstream raw(path, std::ios::binary);
    raw.write(reinterpret_cast<const char*>(image.data()), image.size());
    require(raw.good(), "Could not save full image");
}
}

int main(int argc, char** argv) {
    @autoreleasepool {
        try {
            require(argc == 4, "Expected <reference|timing> <MSL file> <fresh output directory>");
            const std::string_view mode = argv[1];
            require(mode == "reference" || mode == "timing", "Unknown comparison mode");
            std::ifstream file(argv[2], std::ios::binary);
            const std::string source{std::istreambuf_iterator<char>(file), {}};
            require(!source.empty(), "Missing common MSL source");
            const std::filesystem::path out = argv[3];
            require(std::filesystem::create_directory(out), "Comparison output must be fresh");
            Host host;
            Renderer renderer(host, source);
            std::ofstream times;
            if (mode == "timing") {
                times.open(out/"samples.csv");
                times << "draws,sample,record_ns,submit_ns,queue_idle_wait_ns\n";
            }
            for (unsigned i = 0; i < draws.size(); ++i) {
                const unsigned x = 5 + i * 4;
                if (mode == "reference") {
                    @autoreleasepool { renderer.frame(draws[i], x); }
                } else {
                    for (unsigned n = 0; n < warmups + samples; ++n) {
                        @autoreleasepool {
                            const auto t = renderer.frame(draws[i], x);
                            if (n >= warmups) times << draws[i] << ',' << n-warmups << ',' << t.record
                                                    << ',' << t.submit << ',' << t.wait << '\n';
                        }
                    }
                }
                @autoreleasepool { verify(renderer.read(), x, out/(std::to_string(draws[i])+".rgba")); }
            }
            require(mode == "reference" || times.good(), "Could not save timing samples");
            std::cout << "{\"adapter\":\"" << adapter << "\",\"mode\":\"" << mode << "\",\"registry_id\":\""
                      << std::hex << std::setw(16) << std::setfill('0') << host.registry << std::dec << "\",\"name\":"
                      << std::quoted(host.name) << ",\"identity\":\"" << identity << "\",\"target_storage\":\""
                      << renderer.storage << "\",\"mac1\":" << (host.mac1 ? "true" : "false")
                      << ",\"mac2\":" << (host.mac2 ? "true" : "false")
                      << ",\"images\":3,\"timing_samples\":" << (mode == "timing" ? 36 : 0);
#if defined(COMPARE_sdl)
            std::cout << ",\"mac1_compatibility\":" << (!host.mac2 && host.mac1 ? "true" : "false");
#endif
#if defined(COMPARE_sdk)
            std::cout << ",\"SDK_source_fingerprint\":\"" << gfx::buildFingerprint() << '"';
#endif
            std::cout << "}\n";
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "FAIL: " << error.what() << '\n';
            return 1;
        }
    }
}
