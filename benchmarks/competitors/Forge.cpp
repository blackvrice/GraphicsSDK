#include "common/Adapter.hpp"
#include <Common_3/Graphics/Interfaces/IGraphics.h>
#include <Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h>
#include <Common_3/Utilities/Interfaces/IFileSystem.h>
#include <Common_3/Utilities/Interfaces/ILog.h>
#define IMEMORY_FROM_HEADER
#include <Common_3/Utilities/Interfaces/IMemory.h>
#undef assume
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

// The application owns this OS callback. A device reset invalidates the run.
extern "C" void requestReset(const ResetDesc*) {
    std::cerr << "Forge requested a device reset; benchmark invalid\n"; std::abort();
}

namespace bench {
const char* forgeBuildFingerprint();
namespace {
using Microsoft::WRL::ComPtr;
std::atomic_uint forgeErrors{};
void logMessage(void*, const char* text) { ++forgeErrors; std::cerr << text << '\n'; }
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void hr(HRESULT value, const char* text) { require(SUCCEEDED(value), text); }

class ForgeAdapter final : public Adapter {
    Renderer* renderer{};
    Queue* queue{};
    CmdPool* pool{};
    Cmd* cmd{};
    Fence* fence{};
    Shader* shader{};
    Pipeline* pipeline{};
    RenderTarget* target{};
    Texture* uploadTexture{};
    bool loader{};
    std::uint32_t uploadWidth{}, uploadHeight{};
    bool uploadHalf{};
    ComPtr<ID3D12InfoQueue> info;

    void check() {
        if (renderer) hr(renderer->mDx.pDevice->GetDeviceRemovedReason(),"Forge D3D12 device removed");
        require(forgeErrors.load() == 0, "The Forge reported an error; see diagnostics");
        if (!info) return;
        for (UINT64 i=0;i<info->GetNumStoredMessages();++i) {
            SIZE_T size{}; hr(info->GetMessage(i,nullptr,&size), "Forge validation message size failed");
            std::vector<std::byte> bytes(size);
            auto message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            hr(info->GetMessage(i,message,&size), "Forge validation message failed");
            if (message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) {
                std::cerr << message->pDescription << '\n';
                throw std::runtime_error("The Forge native D3D12 validation failed");
            }
        }
        info->ClearStoredMessages();
    }
    void ensureLoader() {
        if (loader) return;
        // One CPU thread; staging pools are SDK managed, as in the other adapters.
        ResourceLoaderDesc desc{}; desc.mBufferSize=128ull*1024*1024;
        desc.mBufferCount=1; desc.mSingleThreaded=true;
        initResourceLoaderInterface(renderer,&desc); loader=true;
        require(isResourceLoaderSingleThreaded(), "Forge loader unexpectedly uses a worker thread");
        check();
    }
    void begin() { resetCmdPool(renderer,pool); beginCmd(cmd); }
    void submit() {
        QueueSubmitDesc desc{}; desc.ppCmds=&cmd; desc.mCmdCount=1; desc.pSignalFence=fence;
        queueSubmit(queue,&desc);
    }
    void complete() { waitForFences(renderer,1,&fence); }
    Pipeline* makePipeline() {
        TinyImageFormat format=TinyImageFormat_R8G8B8A8_UNORM;
        RasterizerStateDesc raster{}; raster.mCullMode=CULL_MODE_NONE;
        raster.mFillMode=FILL_MODE_SOLID; raster.mFrontFace=FRONT_FACE_CCW; raster.mDepthClampEnable=false;
        DepthStateDesc depth{}; depth.mDepthTest=false; depth.mDepthWrite=false;
        PipelineDesc desc{}; desc.mType=PIPELINE_TYPE_GRAPHICS;
        auto& graphics=desc.mGraphicsDesc;
        graphics.pShaderProgram=shader; graphics.pRasterizerState=&raster; graphics.pDepthState=&depth;
        graphics.pColorFormats=&format; graphics.mRenderTargetCount=1;
        graphics.mSampleCount=SAMPLE_COUNT_1; graphics.mPrimitiveTopo=PRIMITIVE_TOPO_TRI_LIST;
        Pipeline* created{}; addPipeline(renderer,&desc,&created);
        require(created && created->mDx.pPipelineState,"Forge native PSO creation failed");
        return created;
    }
    std::vector<std::byte> download(Texture* texture,std::uint32_t w,std::uint32_t h,bool half,ResourceState state) {
        ensureLoader(); idle();
        auto native=texture->mDx.pResource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT rows{}; UINT64 rowSize{},size{};
        renderer->mDx.pDevice->GetCopyableFootprints(&native,0,1,0,&footprint,&rows,&rowSize,&size);
        const auto rowBytes=w*(half?8u:4u);
        require(rows==h && rowSize==rowBytes,"Forge readback footprint mismatch");
        Buffer* staging{}; BufferLoadDesc load{}; load.ppBuffer=&staging;
        load.mDesc.mSize=size; load.mDesc.mMemoryUsage=RESOURCE_MEMORY_USAGE_GPU_TO_CPU;
        load.mDesc.mStartState=RESOURCE_STATE_COPY_DEST;
        load.mDesc.mFlags=BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        addResource(&load,nullptr); require(staging!=nullptr,"Forge readback buffer failed");
        // Untimed correctness probe through documented native handles. The pinned
        // loader's copyResource transfer-queue path fails the current native debug
        // layer's layout checks. Keep upstream untouched; use the same direct queue
        // and Forge barriers/fence, with only the readback copy native.
        begin(); TextureBarrier barrier{texture,state,RESOURCE_STATE_COPY_SOURCE};
        cmdResourceBarrier(cmd,0,nullptr,1,&barrier,0,nullptr);
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource=texture->mDx.pResource;
        src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource=staging->mDx.pResource;
        dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint=footprint;
        cmd->mDx.pCmdList->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        barrier={texture,RESOURCE_STATE_COPY_SOURCE,state};
        cmdResourceBarrier(cmd,0,nullptr,1,&barrier,0,nullptr);
        endCmd(cmd); submit(); complete();
        require(staging->pCpuMappedAddress!=nullptr,"Forge readback map failed");
        std::vector<std::byte> result(static_cast<size_t>(rowBytes)*h);
        for (std::uint32_t y=0;y<h;++y)
            std::memcpy(result.data()+static_cast<size_t>(y)*rowBytes,
                static_cast<const std::byte*>(staging->pCpuMappedAddress)+footprint.Offset+static_cast<size_t>(y)*footprint.Footprint.RowPitch,rowBytes);
        removeResource(staging); check(); return result;
    }
public:
    explicit ForgeAdapter(bool validation) {
        require(initMemAlloc("GraphicsSDKBenchmark"),"Forge memory initialization failed");
        FileSystemInitDesc fs{}; fs.pAppName="GraphicsSDKBenchmark"; fs.mIsTool=true;
        require(initFileSystem(&fs),"Forge filesystem initialization failed");
        initLog(nullptr,eERROR); addLogCallback("benchmark-errors",eERROR,nullptr,logMessage,nullptr,nullptr);
        _EnableInteractiveMode(false);
        // The reviewed upstream GPU selection/configuration files are copied by CMake.
        wchar_t executable[32768]{};
        require(GetModuleFileNameW(nullptr,executable,32768)>0,"Forge executable path failed");
        auto config=(std::filesystem::path(executable).parent_path()/"forge-config").generic_string()+"/";
        fsSetPathForResourceDir(pSystemFileIO,RD_GPU_CONFIG,config.c_str());
        fsSetPathForResourceDir(pSystemFileIO,RD_OTHER_FILES,config.c_str());
        initGPUConfiguration(nullptr);
        if (validation) { ComPtr<ID3D12Debug> debug; hr(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"D3D12 debug layer unavailable"); debug->EnableDebugLayer(); }
        RendererDesc desc{}; desc.mDx.mFeatureLevel=D3D_FEATURE_LEVEL_11_0; desc.mShaderTarget=SHADER_TARGET_5_1;
        initRenderer("GraphicsSDKBenchmark",&desc,&renderer);
        require(renderer && renderer->mDx.pDevice,"Forge D3D12 initialization failed");
        auto luid=renderer->mDx.pDevice->GetAdapterLuid();
        auto identity=std::to_string(luid.HighPart)+":"+std::to_string(luid.LowPart);
        require(environment(deviceName()).find("\"luid\":"+quote(identity))!=std::string::npos,"Forge actual device differs from reported GPU identity");
        if (validation) hr(renderer->mDx.pDevice->QueryInterface(IID_PPV_ARGS(&info)),"Forge native info queue unavailable");
        QueueDesc q{}; q.mType=QUEUE_TYPE_GRAPHICS; initQueue(renderer,&q,&queue);
        require(queue && queue->mDx.pQueue,"Forge graphics queue failed");
        CmdPoolDesc p{}; p.pQueue=queue; initCmdPool(renderer,&p,&pool);
        CmdDesc c{}; c.pPool=pool; initCmd(renderer,&c,&cmd); initFence(renderer,&fence);
        require(pool && cmd && fence,"Forge command resources failed"); check();
    }
    ~ForgeAdapter() override {
        try {
            idle();
            if (pipeline) removePipeline(renderer,pipeline);
            if (shader) removeShader(renderer,shader);
            if (target) removeRenderTarget(renderer,target);
            if (uploadTexture) removeResource(uploadTexture);
            if (loader) exitResourceLoaderInterface(renderer);
            if (renderer->mDx.pGraphicsRootSignature) { renderer->mDx.pGraphicsRootSignature->Release(); renderer->mDx.pGraphicsRootSignature=nullptr; }
            exitFence(renderer,fence); exitCmd(renderer,cmd); exitCmdPool(renderer,pool); exitQueue(renderer,queue);
            check(); exitRenderer(renderer); renderer=nullptr; check();
            info.Reset(); exitGPUConfiguration(); exitLog(); exitFileSystem(); exitMemAlloc();
        } catch (const std::exception& error) { std::cerr << "Forge teardown: " << error.what() << '\n'; std::abort(); }
    }
    std::string deviceName() const override { return renderer->pGpu->mGpuVendorPreset.mGpuName; }
    std::string sourceFingerprint() const override { return forgeBuildFingerprint(); }
    void idle() override {
        ensureLoader();
        waitQueueIdle(queue);
        if (loader) { waitForAllResourceLoads(); waitCopyQueueIdle(); }
        check();
    }
    void* buffer(std::uint32_t bytes) override {
        ensureLoader(); Buffer* result{}; BufferLoadDesc load{}; load.ppBuffer=&result;
        load.mDesc.mSize=bytes; load.mDesc.mMemoryUsage=RESOURCE_MEMORY_USAGE_GPU_ONLY;
        load.mDesc.mDescriptors=DESCRIPTOR_TYPE_VERTEX_BUFFER;
        load.mDesc.mStartState=RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        addResource(&load,nullptr); require(result!=nullptr,"Forge buffer allocation failed"); return result;
    }
    void releaseBuffer(void* ptr) override { removeResource(static_cast<Buffer*>(ptr)); }
    void* texture(std::uint32_t w,std::uint32_t h,bool half) override {
        ensureLoader(); Texture* result{};
        TextureDesc desc{}; desc.mWidth=w; desc.mHeight=h; desc.mDepth=1; desc.mArraySize=1; desc.mMipLevels=1;
        desc.mSampleCount=SAMPLE_COUNT_1; desc.mFormat=half?TinyImageFormat_R16G16B16A16_SFLOAT:TinyImageFormat_R8G8B8A8_UNORM;
        desc.mDescriptors=DESCRIPTOR_TYPE_TEXTURE; desc.mStartState=RESOURCE_STATE_COMMON;
        TextureLoadDesc load{}; load.pDesc=&desc; load.ppTexture=&result;
        addResource(&load,nullptr); require(result!=nullptr,"Forge texture allocation failed"); return result;
    }
    void releaseTexture(void* ptr) override { removeResource(static_cast<Texture*>(ptr)); }
    void prepareDraw(const Shaders& shaders) override {
        // This public Renderer field is explicitly documented as user-filled.
        // The workload has no bindings, so bootstrap an empty native signature;
        // all PSOs, binding and draw commands below use the Forge API.
        D3D12_ROOT_SIGNATURE_DESC root{}; root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> blob,error;
        hr(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Forge empty root serialization failed");
        hr(renderer->mDx.pDevice->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&renderer->mDx.pGraphicsRootSignature)),"Forge empty root creation failed");
        BinaryShaderDesc binary{}; binary.mStages=static_cast<ShaderStage>(SHADER_STAGE_VERT|SHADER_STAGE_FRAG);
        binary.mVert.pByteCode=const_cast<std::byte*>(shaders.vertex.data()); binary.mVert.mByteCodeSize=static_cast<uint32_t>(shaders.vertex.size());
        binary.mFrag.pByteCode=const_cast<std::byte*>(shaders.fragment.data()); binary.mFrag.mByteCodeSize=static_cast<uint32_t>(shaders.fragment.size());
        addShaderBinary(renderer,&binary,&shader); require(shader!=nullptr,"Forge shader creation failed");
        RenderTargetDesc rt{}; rt.mWidth=64; rt.mHeight=64; rt.mDepth=1; rt.mArraySize=1; rt.mMipLevels=1;
        rt.mSampleCount=SAMPLE_COUNT_1; rt.mFormat=TinyImageFormat_R8G8B8A8_UNORM;
        rt.mStartState=RESOURCE_STATE_RENDER_TARGET; rt.mClearValue={{0,0,1,1}};
        addRenderTarget(renderer,&rt,&target); require(target!=nullptr,"Forge target failed");
        pipeline=makePipeline(); check();
    }
    DrawTimes draw(std::uint32_t count,bool instanced) override {
        auto start=Clock::now(); begin();
        BindRenderTargetsDesc render{}; render.mRenderTargetCount=1;
        render.mRenderTargets[0].pRenderTarget=target;
        render.mRenderTargets[0].mLoadAction=LOAD_ACTION_CLEAR;
        render.mRenderTargets[0].mStoreAction=STORE_ACTION_STORE;
        cmdBindRenderTargets(cmd,&render); cmdSetViewport(cmd,0,0,64,64,0,1); cmdSetScissor(cmd,0,0,64,64);
        cmdBindPipeline(cmd,pipeline);
        if (instanced) cmdDrawInstanced(cmd,3,0,count,0); else for (std::uint32_t i=0;i<count;++i) cmdDraw(cmd,3,0);
        cmdBindRenderTargets(cmd,nullptr); endCmd(cmd); auto recorded=Clock::now();
        submit(); auto submitted=Clock::now(); complete(); auto end=Clock::now(); check();
        return {ms(start,recorded),ms(recorded,submitted),ms(submitted,end),ms(start,end),std::nullopt};
    }
    bool verifyDraw() override {
        auto pixels=download(target->pTexture,64,64,false,RESOURCE_STATE_RENDER_TARGET); auto center=(32*64+32)*4;
        return pixels[center]==std::byte{255} && pixels[center+2]==std::byte{0} && pixels[0]==std::byte{0} && pixels[2]==std::byte{255};
    }
    PipelineTimes recreatePipeline() override {
        auto start=Clock::now(); auto created=makePipeline(); auto end=Clock::now();
        auto releasing=Clock::now(); removePipeline(renderer,created); auto released=Clock::now(); check();
        return {ms(start,end),ms(releasing,released)};
    }
    void prepareUpload(std::uint32_t w,std::uint32_t h,bool half) override {
        uploadWidth=w; uploadHeight=h; uploadHalf=half; uploadTexture=static_cast<Texture*>(texture(w,h,half));
    }
    void upload(std::span<const std::byte> pixels) override {
        TextureUpdateDesc update{}; update.pTexture=uploadTexture; update.mMipLevels=1; update.mLayerCount=1;
        update.mCurrentState=RESOURCE_STATE_COMMON; beginUpdateResource(&update);
        auto region=update.getSubresourceUpdateDesc(0,0);
        const auto rowBytes=uploadWidth*(uploadHalf?8u:4u);
        require(pixels.size()==static_cast<size_t>(rowBytes)*uploadHeight && region.mRowCount==uploadHeight && region.mDstRowStride>=rowBytes,"Forge upload layout mismatch");
        if (region.mDstRowStride==rowBytes) std::memcpy(region.pMappedData,pixels.data(),pixels.size());
        else for (std::uint32_t y=0;y<uploadHeight;++y) std::memcpy(region.pMappedData+static_cast<size_t>(y)*region.mDstRowStride,pixels.data()+static_cast<size_t>(y)*rowBytes,rowBytes);
        endUpdateResource(&update); FlushResourceUpdateDesc flush{}; flushResourceUpdates(&flush);
        require(flush.pOutFence!=nullptr,"Forge upload flush omitted completion fence");
        waitForFences(renderer,1,&flush.pOutFence); check();
    }
    bool verifyUpload(std::span<const std::byte> pixels) override {
        auto actual=download(uploadTexture,uploadWidth,uploadHeight,uploadHalf,RESOURCE_STATE_COMMON);
        return std::equal(pixels.begin(),pixels.end(),actual.begin(),actual.end());
    }
};
}
std::unique_ptr<Adapter> forgeAdapter(bool validation) { return std::make_unique<ForgeAdapter>(validation); }
}
