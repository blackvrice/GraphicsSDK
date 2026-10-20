#include "common/Adapter.hpp"
#include <LLGL/LLGL.h>
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
public:
    explicit LLGLAdapter(bool validation) {
        LLGL::RenderSystemDescriptor desc{"Direct3D12"};
        if (validation) desc.flags = LLGL::RenderSystemFlags::DebugDevice;
        LLGL::Report report;
        device = LLGL::RenderSystem::Load(desc, &report);
        if (!device) throw std::runtime_error(report.GetText());
    }
    ~LLGLAdapter() override { device->GetCommandQueue()->WaitIdle(); }
    std::string deviceName() const override { return device->GetRendererInfo().deviceName.c_str(); }
    void idle() override { device->GetCommandQueue()->WaitIdle(); }
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
        if (instanced) commands->DrawInstanced(3,0,count); else for (std::uint32_t i=0;i<count;++i) commands->Draw(3,0);
        commands->EndRenderPass(); commands->End(); auto recorded=Clock::now();
        device->GetCommandQueue()->Submit(*commands); auto submitted=Clock::now(); idle(); auto end=Clock::now();
        return {ms(start,recorded),ms(recorded,submitted),ms(submitted,end),ms(start,end),std::nullopt};
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
