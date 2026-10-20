#include "common/Adapter.hpp"
#include <SDL3/SDL.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace bench {
class SDLAdapter final : public Adapter {
    SDL_GPUDevice* device{};
    SDL_GPUTexture *target{}, *uploadTexture{};
    SDL_GPUShader *vertex{}, *fragment{};
    SDL_GPUGraphicsPipeline* pipeline{};
    SDL_GPUTransferBuffer* staging{};
    std::uint32_t uploadWidth{},uploadHeight{},uploadBpp{};
public:
    explicit SDLAdapter(bool validation) {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXBC, validation, "direct3d12");
        if (!device) { std::string err = SDL_GetError(); SDL_Quit(); throw std::runtime_error(err); }
    }
    ~SDLAdapter() override {
        SDL_WaitForGPUIdle(device);
        if (pipeline) SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
        if (vertex) SDL_ReleaseGPUShader(device,vertex); if (fragment) SDL_ReleaseGPUShader(device,fragment);
        if (target) SDL_ReleaseGPUTexture(device,target); if (uploadTexture) SDL_ReleaseGPUTexture(device,uploadTexture);
        if (staging) SDL_ReleaseGPUTransferBuffer(device,staging);
        SDL_DestroyGPUDevice(device); SDL_Quit();
    }
    std::string deviceName() const override { return SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device), SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"); }
    void idle() override { if (!SDL_WaitForGPUIdle(device)) throw std::runtime_error(SDL_GetError()); }
    void* buffer(std::uint32_t bytes) override {
        SDL_GPUBufferCreateInfo info{SDL_GPU_BUFFERUSAGE_VERTEX, bytes, 0};
        auto ptr = SDL_CreateGPUBuffer(device, &info);
        if (!ptr) throw std::runtime_error(SDL_GetError());
        return ptr;
    }
    void releaseBuffer(void* ptr) override { SDL_ReleaseGPUBuffer(device, static_cast<SDL_GPUBuffer*>(ptr)); }
    void* texture(std::uint32_t w, std::uint32_t h, bool half) override {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = half ? SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = w; info.height = h; info.layer_count_or_depth = 1; info.num_levels = 1;
        auto ptr = SDL_CreateGPUTexture(device, &info);
        if (!ptr) throw std::runtime_error(SDL_GetError());
        return ptr;
    }
    void releaseTexture(void* ptr) override { SDL_ReleaseGPUTexture(device, static_cast<SDL_GPUTexture*>(ptr)); }
    void prepareDraw(const Shaders& shaders) override {
        SDL_GPUTextureCreateInfo info{}; info.type=SDL_GPU_TEXTURETYPE_2D; info.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET; info.width=64; info.height=64; info.layer_count_or_depth=1; info.num_levels=1;
        target=SDL_CreateGPUTexture(device,&info); if (!target) throw std::runtime_error(SDL_GetError());
        SDL_GPUShaderCreateInfo shader{}; shader.format=SDL_GPU_SHADERFORMAT_DXBC;
        shader.code_size=shaders.vertex.size(); shader.code=reinterpret_cast<const Uint8*>(shaders.vertex.data()); shader.stage=SDL_GPU_SHADERSTAGE_VERTEX; shader.entrypoint="VS";
        vertex=SDL_CreateGPUShader(device,&shader);
        shader.code_size=shaders.fragment.size(); shader.code=reinterpret_cast<const Uint8*>(shaders.fragment.data()); shader.stage=SDL_GPU_SHADERSTAGE_FRAGMENT; shader.entrypoint="PS";
        fragment=SDL_CreateGPUShader(device,&shader); if (!vertex || !fragment) throw std::runtime_error(SDL_GetError());
        SDL_GPUColorTargetDescription color{}; color.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUGraphicsPipelineCreateInfo desc{}; desc.vertex_shader=vertex; desc.fragment_shader=fragment;
        desc.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; desc.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE; desc.rasterizer_state.enable_depth_clip=true;
        desc.target_info.color_target_descriptions=&color; desc.target_info.num_color_targets=1;
        pipeline=SDL_CreateGPUGraphicsPipeline(device,&desc); if (!pipeline) throw std::runtime_error(SDL_GetError());
    }
    DrawTimes draw(std::uint32_t count,bool instanced) override {
        auto start=Clock::now(); auto cmd=SDL_AcquireGPUCommandBuffer(device); if (!cmd) throw std::runtime_error(SDL_GetError());
        SDL_GPUColorTargetInfo color{}; color.texture=target; color.clear_color={0,0,1,1}; color.load_op=SDL_GPU_LOADOP_CLEAR; color.store_op=SDL_GPU_STOREOP_STORE;
        auto pass=SDL_BeginGPURenderPass(cmd,&color,1,nullptr); if (!pass) throw std::runtime_error(SDL_GetError());
        SDL_BindGPUGraphicsPipeline(pass,pipeline);
        if (instanced) SDL_DrawGPUPrimitives(pass,3,count,0,0); else for (std::uint32_t i=0;i<count;++i) SDL_DrawGPUPrimitives(pass,3,1,0,0);
        SDL_EndGPURenderPass(pass); auto recorded=Clock::now();
        if (!SDL_SubmitGPUCommandBuffer(cmd)) throw std::runtime_error(SDL_GetError()); auto submitted=Clock::now(); idle(); auto end=Clock::now();
        return {ms(start,recorded),ms(recorded,submitted),ms(submitted,end),ms(start,end),std::nullopt};
    }
    std::vector<std::byte> download(SDL_GPUTexture* texture,std::uint32_t w,std::uint32_t h,std::uint32_t bpp) {
        SDL_GPUTransferBufferCreateInfo info{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,w*h*bpp,0};
        auto transfer=SDL_CreateGPUTransferBuffer(device,&info); if (!transfer) throw std::runtime_error(SDL_GetError());
        auto cmd=SDL_AcquireGPUCommandBuffer(device); if (!cmd) throw std::runtime_error(SDL_GetError());
        auto pass=SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion region{}; region.texture=texture; region.w=w; region.h=h; region.d=1;
        SDL_GPUTextureTransferInfo location{transfer,0,w,h}; SDL_DownloadFromGPUTexture(pass,&region,&location); SDL_EndGPUCopyPass(pass);
        if (!SDL_SubmitGPUCommandBuffer(cmd)) throw std::runtime_error(SDL_GetError()); idle();
        auto data=SDL_MapGPUTransferBuffer(device,transfer,false); if (!data) throw std::runtime_error(SDL_GetError());
        std::vector<std::byte> bytes(static_cast<size_t>(w)*h*bpp); std::memcpy(bytes.data(),data,bytes.size());
        SDL_UnmapGPUTransferBuffer(device,transfer); SDL_ReleaseGPUTransferBuffer(device,transfer); return bytes;
    }
    bool verifyDraw() override {
        auto pixels=download(target,64,64,4); size_t center=(32*64+32)*4;
        return pixels[center]==std::byte{255} && pixels[center+2]==std::byte{0} && pixels[0]==std::byte{0} && pixels[2]==std::byte{255};
    }
    PipelineTimes recreatePipeline() override {
        SDL_GPUColorTargetDescription color{}; color.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUGraphicsPipelineCreateInfo desc{}; desc.vertex_shader=vertex; desc.fragment_shader=fragment;
        desc.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; desc.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE; desc.rasterizer_state.enable_depth_clip=true;
        desc.target_info.color_target_descriptions=&color; desc.target_info.num_color_targets=1;
        auto start=Clock::now(); auto created=SDL_CreateGPUGraphicsPipeline(device,&desc); auto end=Clock::now();
        if (!created) throw std::runtime_error(SDL_GetError()); auto releasing=Clock::now(); SDL_ReleaseGPUGraphicsPipeline(device,created); auto released=Clock::now();
        return {ms(start,end),ms(releasing,released)};
    }
    void prepareUpload(std::uint32_t w,std::uint32_t h,bool half) override {
        uploadWidth=w; uploadHeight=h; uploadBpp=half?8:4;
        uploadTexture=static_cast<SDL_GPUTexture*>(texture(w,h,half));
        SDL_GPUTransferBufferCreateInfo info{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,w*h*uploadBpp,0};
        staging=SDL_CreateGPUTransferBuffer(device,&info); if (!staging) throw std::runtime_error(SDL_GetError());
    }
    void upload(std::span<const std::byte> pixels) override {
        auto data=SDL_MapGPUTransferBuffer(device,staging,false); if (!data) throw std::runtime_error(SDL_GetError()); std::memcpy(data,pixels.data(),pixels.size()); SDL_UnmapGPUTransferBuffer(device,staging);
        auto cmd=SDL_AcquireGPUCommandBuffer(device); if (!cmd) throw std::runtime_error(SDL_GetError()); auto pass=SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo location{staging,0,uploadWidth,uploadHeight};
        SDL_GPUTextureRegion region{}; region.texture=uploadTexture; region.w=uploadWidth; region.h=uploadHeight; region.d=1;
        SDL_UploadToGPUTexture(pass,&location,&region,false); SDL_EndGPUCopyPass(pass);
        if (!SDL_SubmitGPUCommandBuffer(cmd)) throw std::runtime_error(SDL_GetError()); idle();
    }
    bool verifyUpload(std::span<const std::byte> pixels) override { auto actual=download(uploadTexture,uploadWidth,uploadHeight,uploadBpp); return std::equal(pixels.begin(),pixels.end(),actual.begin(),actual.end()); }
};
std::unique_ptr<Adapter> sdl(bool validation) { return std::make_unique<SDLAdapter>(validation); }
}
