#include "common/Adapter.hpp"
#include <graphics/Graphics.hpp>
#include <cstring>
#include <stdexcept>

namespace bench {
template<class T> T require(gfx::Result<T> value) { if (!value) throw std::runtime_error(value.error().message); return std::move(*value); }
void require(gfx::Result<void> value) { if (!value) throw std::runtime_error(value.error().message); }
class GraphicsSDKAdapter final : public Adapter {
    std::unique_ptr<gfx::Device> device;
    gfx::TexturePtr target, uploadTexture;
    gfx::PipelinePtr pipeline;
    gfx::BufferPtr staging;
    std::unique_ptr<gfx::CommandList> commands;
    std::uint32_t uploadWidth{}, uploadHeight{}, bytesPerPixel{};
    gfx::GraphicsPipelineDesc pipelineDesc;
public:
    explicit GraphicsSDKAdapter(bool validation) : device(require(gfx::createDevice({gfx::Backend::D3D12,validation}))) {}
    ~GraphicsSDKAdapter() override { auto status = device->queue().waitIdle(); (void)status; }
    std::string deviceName() const override { return device->caps().adapterName; }
    std::string sourceFingerprint() const override { return std::string(gfx::buildFingerprint()); }
    void idle() override { require(device->queue().waitIdle()); }
    void* buffer(std::uint32_t bytes) override { return new gfx::BufferPtr(require(device->createBuffer({bytes}))); }
    void releaseBuffer(void* ptr) override { delete static_cast<gfx::BufferPtr*>(ptr); }
    void* texture(std::uint32_t w, std::uint32_t h, bool half) override { return new gfx::TexturePtr(require(device->createTexture({w,h,half ? gfx::Format::RGBA16Float:gfx::Format::RGBA8UNorm}))); }
    void releaseTexture(void* ptr) override { delete static_cast<gfx::TexturePtr*>(ptr); }
    void prepareDraw(const Shaders& shaders) override {
        target = require(device->createTexture({64,64,gfx::Format::RGBA8UNorm,true}));
        pipelineDesc={{gfx::ShaderStage::Vertex,shaders.vertex},{gfx::ShaderStage::Fragment,shaders.fragment}};
        pipeline = require(device->createGraphicsPipeline(pipelineDesc));
        commands = require(device->createCommandList());
    }
    DrawTimes draw(std::uint32_t count, bool instanced) override {
        auto start = Clock::now();
        require(commands->begin());
        require(commands->transition(target,gfx::ResourceState::RenderTarget)); require(commands->beginRenderPass(target,{0,0,1,1})); require(commands->setPipeline(pipeline));
        if (instanced) require(commands->draw(3,count)); else for (std::uint32_t i=0;i<count;++i) require(commands->draw(3));
        require(commands->endRenderPass()); require(commands->end());
        auto recorded = Clock::now(); auto fence = require(device->queue().submit(*commands)); auto submitted = Clock::now();
        require(device->queue().wait(fence)); auto end = Clock::now();
        return {ms(start,recorded),ms(recorded,submitted),ms(submitted,end),ms(start,end),std::nullopt};
    }
    std::vector<std::byte> download(const gfx::TexturePtr& texture) {
        auto layout = texture->footprint(); auto readback = require(device->createBuffer({layout.size,gfx::Memory::Readback}));
        auto cmd = require(device->createCommandList()); require(cmd->begin()); require(cmd->transition(texture,gfx::ResourceState::CopySource)); require(cmd->copyTextureToBuffer(texture,readback)); require(cmd->end());
        auto fence = require(device->queue().submit(*cmd)); require(device->queue().wait(fence)); auto padded = require(readback->read(0,layout.size));
        std::vector<std::byte> tight(static_cast<size_t>(layout.rowBytes)*layout.height);
        for (std::uint32_t y=0;y<layout.height;++y) std::memcpy(tight.data()+y*layout.rowBytes,padded.data()+y*layout.rowPitch,layout.rowBytes);
        return tight;
    }
    bool verifyDraw() override {
        auto pixels = download(target); size_t center = (32*64+32)*4;
        return pixels[center] == std::byte{255} && pixels[center+2] == std::byte{0} && pixels[0] == std::byte{0} && pixels[2] == std::byte{255};
    }
    PipelineTimes recreatePipeline() override {
        auto start=Clock::now(); auto created=require(device->createGraphicsPipeline(pipelineDesc)); auto end=Clock::now();
        auto releasing=Clock::now(); created.reset(); auto released=Clock::now(); return {ms(start,end),ms(releasing,released)};
    }
    void prepareUpload(std::uint32_t w,std::uint32_t h,bool half) override {
        uploadWidth=w; uploadHeight=h; bytesPerPixel=half?8:4;
        uploadTexture=require(device->createTexture({w,h,half?gfx::Format::RGBA16Float:gfx::Format::RGBA8UNorm}));
        staging=require(device->createBuffer({uploadTexture->footprint().size,gfx::Memory::Upload}));
        commands=require(device->createCommandList());
    }
    void upload(std::span<const std::byte> pixels) override {
        auto layout=uploadTexture->footprint();
        // Selected benchmark widths have aligned rows; reject accidental contract changes.
        if (layout.rowPitch != uploadWidth*bytesPerPixel || pixels.size()!=static_cast<size_t>(uploadWidth)*uploadHeight*bytesPerPixel) throw std::runtime_error("Upload row pitch contract mismatch");
        require(staging->write(0,pixels)); require(commands->begin()); require(commands->transition(uploadTexture,gfx::ResourceState::CopyDestination));
        require(commands->copyBufferToTexture(staging,uploadTexture)); require(commands->end());
        auto fence=require(device->queue().submit(*commands)); require(device->queue().wait(fence));
    }
    bool verifyUpload(std::span<const std::byte> pixels) override { auto actual=download(uploadTexture); return std::equal(pixels.begin(),pixels.end(),actual.begin(),actual.end()); }
};
std::unique_ptr<Adapter> graphicsSDK(bool validation) { return std::make_unique<GraphicsSDKAdapter>(validation); }
}
