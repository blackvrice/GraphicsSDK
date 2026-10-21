#include "common/Adapter.hpp"
#include "common/GpuTimestamp.hpp"
#include <LLGL/LLGL.h>
#include <LLGL/Backend/Direct3D12/NativeHandle.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>
#include <algorithm>

namespace bench {
class LLGLAdapter final : public Adapter {
    LLGL::RenderSystemPtr device;
    LLGL::Texture *targetTexture{}, *uploadTexture{};
    LLGL::RenderTarget* target{};
    LLGL::CommandBuffer* commands{};
    LLGL::PipelineState* pipeline{};
    LLGL::Shader *vertexShader{}, *fragmentShader{};
    std::uint32_t uploadWidth{}, uploadHeight{};
    bool uploadHalf{};
    LLGL::QueryHeap* timestamps{};
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;
    void checkValidation() {
        if (!info) return;
        for (UINT64 i=0;i<info->GetNumStoredMessages();++i) {
            SIZE_T size{};
            if (FAILED(info->GetMessage(i,nullptr,&size))) throw std::runtime_error("LLGL validation message size failed");
            std::vector<std::byte> bytes(size); auto message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            if (FAILED(info->GetMessage(i,message,&size))) throw std::runtime_error("LLGL validation message failed");
            if (message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) {
                std::cerr << message->pDescription << '\n'; throw std::runtime_error("LLGL native D3D12 validation failed");
            }
        }
        info->ClearStoredMessages();
    }
public:
    explicit LLGLAdapter(bool validation) {
        LLGL::RenderSystemDescriptor desc{"Direct3D12"};
        if (validation) desc.flags = LLGL::RenderSystemFlags::DebugDevice;
        LLGL::Report report;
        device = LLGL::RenderSystem::Load(desc, &report);
        if (!device) throw std::runtime_error(report.GetText());
        if (validation) {
            LLGL::Direct3D12::RenderSystemNativeHandle native{};
            if (!device->GetNativeHandle(&native,sizeof(native)) || !native.device) throw std::runtime_error("LLGL native device unavailable");
            auto status=native.device->QueryInterface(IID_PPV_ARGS(&info));
            native.device->Release(); if (native.factory) native.factory->Release(); if (native.commandQueue) native.commandQueue->Release();
            if (FAILED(status)) throw std::runtime_error("LLGL native debug info queue unavailable");
            checkValidation();
        }
    }
    ~LLGLAdapter() override {
        try { idle(); if (timestamps) device->Release(*timestamps); device.reset(); checkValidation(); }
        catch (const std::exception& error) { std::cerr << "LLGL teardown: " << error.what() << '\n'; std::abort(); }
    }
    void enableGpuTimestamps() override {
        LLGL::QueryHeapDescriptor desc; desc.type=LLGL::QueryType::TimeElapsed; desc.numQueries=1;
        timestamps=device->CreateQueryHeap(desc);
        if (!timestamps) throw std::runtime_error("LLGL elapsed timestamp query unavailable");
    }
    std::string deviceName() const override { return device->GetRendererInfo().deviceName.c_str(); }
    std::string unsupportedReason(std::string_view scenario) const override {
        if (scenario=="upload1080" || scenario=="upload4k" || scenario=="upload4k16f")
            return "Pinned LLGL full-region WriteTexture after ReadTexture lacks COPY_SOURCE to COPY_DEST transition; repeated upload validation is blocked";
        return {};
    }
    void idle() override { device->GetCommandQueue()->WaitIdle(); checkValidation(); }
    void* buffer(std::uint32_t bytes) override {
        LLGL::BufferDescriptor desc;
        desc.size = bytes; desc.bindFlags = LLGL::BindFlags::VertexBuffer;
        auto ptr = device->CreateBuffer(desc);
        if (!ptr) throw std::runtime_error("LLGL buffer allocation failed");
        return ptr;
    }
    void releaseBuffer(void* ptr) override { device->Release(*static_cast<LLGL::Buffer*>(ptr)); }
    void* texture(std::uint32_t w, std::uint32_t h, bool half) override {
        LLGL::TextureDescriptor desc;
        desc.type = LLGL::TextureType::Texture2D;
        desc.extent = {w, h, 1}; desc.mipLevels = 1;
        desc.format = half ? LLGL::Format::RGBA16Float : LLGL::Format::RGBA8UNorm;
        desc.bindFlags = LLGL::BindFlags::Sampled;
        desc.miscFlags = LLGL::MiscFlags::NoInitialData;
        auto ptr = device->CreateTexture(desc);
        if (!ptr) throw std::runtime_error("LLGL texture allocation failed");
        return ptr;
    }
    void releaseTexture(void* ptr) override { device->Release(*static_cast<LLGL::Texture*>(ptr)); }
    void prepareDraw(const Shaders& shaders) override {
        LLGL::TextureDescriptor textureDesc;
        textureDesc.extent={64,64,1}; textureDesc.format=LLGL::Format::RGBA8UNorm; textureDesc.mipLevels=1;
        textureDesc.bindFlags=LLGL::BindFlags::ColorAttachment | LLGL::BindFlags::CopySrc;
        textureDesc.miscFlags=LLGL::MiscFlags::NoInitialData;
        targetTexture=device->CreateTexture(textureDesc);
        LLGL::RenderTargetDescriptor targetDesc; targetDesc.resolution={64,64}; targetDesc.colorAttachments[0]=LLGL::AttachmentDescriptor{targetTexture};
        target=device->CreateRenderTarget(targetDesc);
        LLGL::ShaderDescriptor vertex, fragment;
        vertex.type=LLGL::ShaderType::Vertex; vertex.source=reinterpret_cast<const char*>(shaders.vertex.data()); vertex.sourceSize=shaders.vertex.size(); vertex.sourceType=LLGL::ShaderSourceType::BinaryBuffer;
        fragment.type=LLGL::ShaderType::Fragment; fragment.source=reinterpret_cast<const char*>(shaders.fragment.data()); fragment.sourceSize=shaders.fragment.size(); fragment.sourceType=LLGL::ShaderSourceType::BinaryBuffer;
        auto vs=device->CreateShader(vertex), ps=device->CreateShader(fragment);
        vertexShader=vs; fragmentShader=ps;
        for (auto shader : {vs,ps}) if (!shader || (shader->GetReport() && shader->GetReport()->HasErrors())) throw std::runtime_error("LLGL shader creation failed");
        LLGL::GraphicsPipelineDescriptor desc; desc.vertexShader=vs; desc.fragmentShader=ps; desc.renderPass=target->GetRenderPass(); desc.rasterizer.cullMode=LLGL::CullMode::Disabled;
        pipeline=device->CreatePipelineState(desc);
        if (!pipeline || (pipeline->GetReport() && pipeline->GetReport()->HasErrors())) throw std::runtime_error("LLGL pipeline creation failed");
        commands=device->CreateCommandBuffer();
        if (!targetTexture || !target || !commands) throw std::runtime_error("LLGL draw setup failed");
    }
    DrawTimes draw(std::uint32_t count,bool instanced) override {
        auto start=Clock::now(); commands->Begin(); commands->SetViewport(LLGL::Extent2D{64,64}); commands->BeginRenderPass(*target);
        const float clear[]{0,0,1,1}; commands->Clear(LLGL::ClearFlags::Color,clear); commands->SetPipelineState(*pipeline);
        if (timestamps) commands->BeginQuery(*timestamps,0);
        if (instanced) commands->DrawInstanced(3,0,count); else for (std::uint32_t i=0;i<count;++i) commands->Draw(3,0);
        if (timestamps) commands->EndQuery(*timestamps,0);
        commands->EndRenderPass(); commands->End(); auto recorded=Clock::now();
        device->GetCommandQueue()->Submit(*commands); auto submitted=Clock::now(); idle(); auto completed=Clock::now();
        std::optional<double> gpu;
        if (timestamps) {
            std::uint64_t nanoseconds{};
            // The public API returns integer nanoseconds; resolving may submit and
            // wait a separate internal command list, outside the GPU interval.
            if (!device->GetCommandQueue()->QueryResult(*timestamps,0,1,&nanoseconds,sizeof(nanoseconds)))
                throw std::runtime_error("LLGL timestamp result unavailable after completion");
            gpu=gpuTicksToMilliseconds(0,nanoseconds,1e9);
        }
        auto end=Clock::now();
        checkValidation();
        return {ms(start,recorded),ms(recorded,submitted),ms(submitted,completed),ms(start,end),gpu,timestamps?ms(completed,end):0};
    }
    std::vector<std::byte> download(LLGL::Texture* texture,std::uint32_t w,std::uint32_t h,bool half) {
        std::vector<std::byte> pixels(static_cast<size_t>(w)*h*(half?8:4));
        LLGL::TextureRegion region; region.extent={w,h,1};
        LLGL::MutableImageView image{LLGL::ImageFormat::RGBA,half?LLGL::DataType::Float16:LLGL::DataType::UInt8,pixels.data(),pixels.size()};
        device->ReadTexture(*texture,region,image); idle(); return pixels;
    }
    bool verifyDraw() override {
        auto pixels=download(targetTexture,64,64,false); size_t center=(32*64+32)*4;
        return pixels[center]==std::byte{255} && pixels[center+2]==std::byte{0} && pixels[0]==std::byte{0} && pixels[2]==std::byte{255};
    }
    PipelineTimes recreatePipeline() override {
        LLGL::GraphicsPipelineDescriptor desc; desc.vertexShader=vertexShader; desc.fragmentShader=fragmentShader;
        desc.renderPass=target->GetRenderPass(); desc.rasterizer.cullMode=LLGL::CullMode::Disabled;
        auto start=Clock::now(); auto created=device->CreatePipelineState(desc); auto end=Clock::now();
        if (!created || (created->GetReport() && created->GetReport()->HasErrors())) throw std::runtime_error("LLGL PSO creation failed");
        auto releasing=Clock::now(); device->Release(*created); auto released=Clock::now();
        return {ms(start,end),ms(releasing,released)};
    }
    void prepareUpload(std::uint32_t w,std::uint32_t h,bool half) override {
        uploadWidth=w; uploadHeight=h; uploadHalf=half;
        LLGL::TextureDescriptor desc; desc.extent={w,h,1}; desc.mipLevels=1; desc.format=half?LLGL::Format::RGBA16Float:LLGL::Format::RGBA8UNorm;
        desc.bindFlags=LLGL::BindFlags::Sampled | LLGL::BindFlags::CopySrc | LLGL::BindFlags::CopyDst; desc.miscFlags=LLGL::MiscFlags::NoInitialData;
        uploadTexture=device->CreateTexture(desc); if (!uploadTexture) throw std::runtime_error("LLGL upload texture creation failed");
    }
    void upload(std::span<const std::byte> pixels) override {
        LLGL::TextureRegion region; region.extent={uploadWidth,uploadHeight,1};
        LLGL::ImageView image{LLGL::ImageFormat::RGBA,uploadHalf?LLGL::DataType::Float16:LLGL::DataType::UInt8,pixels.data(),pixels.size()};
        device->WriteTexture(*uploadTexture,region,image); idle();
    }
    bool verifyUpload(std::span<const std::byte> pixels) override {
        auto actual=download(uploadTexture,uploadWidth,uploadHeight,uploadHalf); return std::equal(pixels.begin(),pixels.end(),actual.begin(),actual.end());
    }
};
std::unique_ptr<Adapter> llgl(bool validation) { return std::make_unique<LLGLAdapter>(validation); }
}
