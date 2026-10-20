#include "common/Adapter.hpp"
#include <d3d12.h>
#include <Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h>
#include <Graphics/GraphicsEngineD3D12/interface/RenderDeviceD3D12.h>
#include <Common/interface/RefCntAutoPtr.hpp>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace bench {
namespace {
using namespace Diligent;
std::atomic_uint errors{};
void DILIGENT_CALL_TYPE message(DEBUG_MESSAGE_SEVERITY severity, const Char* text, const Char*, const Char*, int) {
    if (severity >= DEBUG_MESSAGE_SEVERITY_ERROR) ++errors;
    std::cerr << "Diligent [" << static_cast<int>(severity) << "]: " << text << '\n';
}
void check() {
    if (errors.load()) throw std::runtime_error("Diligent reported an error; see diagnostics");
}

class DiligentAdapter final : public Adapter {
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    RefCntAutoPtr<ITexture> target, uploadTarget;
    RefCntAutoPtr<IShader> vertex, fragment;
    RefCntAutoPtr<IPipelineState> pipeline;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;

    void checkValidation() {
        if (!infoQueue) return;
        for (UINT64 i=0;i<infoQueue->GetNumStoredMessages();++i) {
            SIZE_T size{}; infoQueue->GetMessage(i,nullptr,&size);
            std::vector<std::byte> storage(size);
            auto entry=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (FAILED(infoQueue->GetMessage(i,entry,&size))) throw std::runtime_error("Diligent validation message query failed");
            if (entry->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) {
                std::cerr << entry->pDescription << '\n';
                throw std::runtime_error("Diligent native D3D12 validation failed");
            }
        }
        infoQueue->ClearStoredMessages();
    }

    GraphicsPipelineStateCreateInfo pipelineInfo() const {
        GraphicsPipelineStateCreateInfo desc;
        desc.PSODesc.Name="bench triangle";
        desc.PSODesc.PipelineType=PIPELINE_TYPE_GRAPHICS;
        desc.GraphicsPipeline.NumRenderTargets=1;
        desc.GraphicsPipeline.RTVFormats[0]=TEX_FORMAT_RGBA8_UNORM;
        desc.GraphicsPipeline.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        desc.GraphicsPipeline.RasterizerDesc.CullMode=CULL_MODE_NONE;
        desc.GraphicsPipeline.DepthStencilDesc.DepthEnable=false;
        desc.GraphicsPipeline.DepthStencilDesc.StencilEnable=false;
        desc.pVS=vertex; desc.pPS=fragment;
        return desc;
    }
    RefCntAutoPtr<ITexture> makeTexture(std::uint32_t w,std::uint32_t h,bool half,Diligent::BIND_FLAGS bind) {
        TextureDesc desc;
        desc.Name="bench texture"; desc.Type=RESOURCE_DIM_TEX_2D;
        desc.Width=w; desc.Height=h; desc.MipLevels=1;
        desc.Format=half?TEX_FORMAT_RGBA16_FLOAT:TEX_FORMAT_RGBA8_UNORM;
        desc.Usage=USAGE_DEFAULT; desc.BindFlags=bind;
        RefCntAutoPtr<ITexture> ptr;
        device->CreateTexture(desc,nullptr,&ptr);
        if (!ptr) throw std::runtime_error("Diligent texture allocation failed");
        return ptr;
    }
    std::vector<std::byte> download(ITexture* source) {
        auto desc=source->GetDesc();
        desc.Name="bench readback"; desc.Usage=USAGE_STAGING; desc.BindFlags=BIND_NONE;
        desc.CPUAccessFlags=CPU_ACCESS_READ; desc.MiscFlags=MISC_TEXTURE_FLAG_NONE;
        RefCntAutoPtr<ITexture> staging;
        device->CreateTexture(desc,nullptr,&staging);
        if (!staging) throw std::runtime_error("Diligent staging texture failed");
        context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);
        context->CopyTexture(CopyTextureAttribs{source,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,staging,RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
        idle();
        MappedTextureSubresource mapped;
        context->MapTextureSubresource(staging,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);
        if (!mapped.pData) throw std::runtime_error("Diligent readback mapping failed");
        const size_t row=static_cast<size_t>(desc.Width)*(desc.Format==TEX_FORMAT_RGBA16_FLOAT?8:4);
        std::vector<std::byte> bytes(row*desc.Height);
        for (std::uint32_t y=0;y<desc.Height;++y)
            std::memcpy(bytes.data()+y*row,static_cast<const std::byte*>(mapped.pData)+y*mapped.Stride,row);
        context->UnmapTextureSubresource(staging,0,0); check();
        return bytes;
    }
public:
    explicit DiligentAdapter(bool validation) {
        errors=0;
        auto factory=LoadAndGetEngineFactoryD3D12();
        if (!factory) throw std::runtime_error("Diligent D3D12 factory could not be loaded");
        factory->SetMessageCallback(message); factory->SetBreakOnError(false);
        EngineD3D12CreateInfo desc;
        desc.SetValidationLevel(validation?VALIDATION_LEVEL_1:VALIDATION_LEVEL_DISABLED);
        desc.D3D12ValidationFlags=D3D12_VALIDATION_FLAG_NONE;
        // The benchmark uses synchronous shaders/PSOs and one immediate graphics context.
        desc.Features.AsyncShaderCompilation=DEVICE_FEATURE_STATE_DISABLED;
        factory->CreateDeviceAndContextsD3D12(desc,&device,&context);
        if (!device || !context) throw std::runtime_error("Diligent D3D12 device creation failed");
        if (validation) {
            RefCntAutoPtr<IRenderDeviceD3D12> native{device,IID_RenderDeviceD3D12};
            if (!native || FAILED(native->GetD3D12Device()->QueryInterface(IID_PPV_ARGS(&infoQueue))))
                throw std::runtime_error("Diligent native D3D12 debug layer is unavailable");
            std::cerr << "Diligent native D3D12 validation enabled; error/corruption messages fail the run\n";
            checkValidation();
        }
        check();
    }
    ~DiligentAdapter() override {
        if (context) {
            context->WaitForIdle(); context->FinishFrame();
            // Context bindings can retain the PSO/RT until the context itself is released.
            context.Release(); pipeline.Release(); target.Release(); uploadTarget.Release();
            vertex.Release(); fragment.Release(); device->ReleaseStaleResources(true);
        }
    }
    std::string deviceName() const override { return device->GetAdapterInfo().Description; }
    std::string sourceFingerprint() const override { return GFX_DILIGENT_BUILD_SHA256; }
    void idle() override {
        context->WaitForIdle(); context->FinishFrame(); device->ReleaseStaleResources(true); check(); checkValidation();
    }
    void* buffer(std::uint32_t bytes) override {
        BufferDesc desc; desc.Name="bench buffer"; desc.Size=bytes; desc.BindFlags=BIND_VERTEX_BUFFER; desc.Usage=USAGE_DEFAULT;
        IBuffer* ptr{}; device->CreateBuffer(desc,nullptr,&ptr);
        if (!ptr) throw std::runtime_error("Diligent buffer allocation failed");
        return ptr;
    }
    void releaseBuffer(void* ptr) override { static_cast<IBuffer*>(ptr)->Release(); }
    void* texture(std::uint32_t w,std::uint32_t h,bool half) override {
        auto ptr=makeTexture(w,h,half,BIND_SHADER_RESOURCE); return ptr.Detach();
    }
    void releaseTexture(void* ptr) override { static_cast<ITexture*>(ptr)->Release(); }
    void prepareDraw(const Shaders& shaders) override {
        target=makeTexture(64,64,false,BIND_RENDER_TARGET);
        ShaderCreateInfo desc;
        desc.Desc.Name="bench vertex"; desc.Desc.ShaderType=SHADER_TYPE_VERTEX;
        desc.ByteCode=shaders.vertex.data(); desc.ByteCodeSize=shaders.vertex.size();
        device->CreateShader(desc,&vertex);
        desc.Desc.Name="bench fragment"; desc.Desc.ShaderType=SHADER_TYPE_PIXEL;
        desc.ByteCode=shaders.fragment.data(); desc.ByteCodeSize=shaders.fragment.size();
        device->CreateShader(desc,&fragment);
        if (!vertex || !fragment) throw std::runtime_error("Diligent bytecode shader creation failed");
        device->CreateGraphicsPipelineState(pipelineInfo(),&pipeline);
        if (!pipeline) throw std::runtime_error("Diligent PSO creation failed");
        check();
    }
    DrawTimes draw(std::uint32_t count,bool instanced) override {
        auto start=Clock::now();
        auto rtv=target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1,&rtv,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport; viewport.Width=64; viewport.Height=64;
        context->SetViewports(1,&viewport,64,64);
        const float clear[]{0,0,1,1};
        context->ClearRenderTarget(rtv,clear,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->SetPipelineState(pipeline);
        if (instanced) context->Draw(DrawAttribs{3,DRAW_FLAG_NONE,count});
        else for (std::uint32_t i=0;i<count;++i) context->Draw(DrawAttribs{3,DRAW_FLAG_NONE});
        auto recorded=Clock::now(); context->Flush(); auto submitted=Clock::now();
        idle(); auto end=Clock::now();
        return {ms(start,recorded),ms(recorded,submitted),ms(submitted,end),ms(start,end),std::nullopt};
    }
    bool verifyDraw() override {
        auto bytes=download(target); const size_t center=(32*64+32)*4;
        return bytes[center]==std::byte{255} && bytes[center+2]==std::byte{0} && bytes[0]==std::byte{0} && bytes[2]==std::byte{255};
    }
    PipelineTimes recreatePipeline() override {
        auto desc=pipelineInfo(); RefCntAutoPtr<IPipelineState> created;
        auto start=Clock::now(); device->CreateGraphicsPipelineState(desc,&created); auto end=Clock::now();
        if (!created) throw std::runtime_error("Diligent repeated PSO creation failed");
        auto releasing=Clock::now(); created.Release(); auto released=Clock::now();
        check(); return {ms(start,end),ms(releasing,released)};
    }
    void prepareUpload(std::uint32_t w,std::uint32_t h,bool half) override { uploadTarget=makeTexture(w,h,half,BIND_SHADER_RESOURCE); }
    void upload(std::span<const std::byte> bytes) override {
        const auto& desc=uploadTarget->GetDesc();
        TextureSubResData data{bytes.data(),static_cast<Uint64>(desc.Width)*(desc.Format==TEX_FORMAT_RGBA16_FLOAT?8:4)};
        context->UpdateTexture(uploadTarget,0,0,Box{0,desc.Width,0,desc.Height,0,1},data,
                               RESOURCE_STATE_TRANSITION_MODE_NONE,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        idle();
    }
    bool verifyUpload(std::span<const std::byte> bytes) override {
        auto actual=download(uploadTarget); return std::equal(bytes.begin(),bytes.end(),actual.begin(),actual.end());
    }
};
}
std::unique_ptr<Adapter> diligent(bool validation) { return std::make_unique<DiligentAdapter>(validation); }
}
