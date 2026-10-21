#include "common/Adapter.hpp"
#include "common/BgfxShader.hpp"
#include <bgfx/bgfx.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace bench {
const char* bgfxBuildFingerprint();
namespace {
using Microsoft::WRL::ComPtr;
void checked(HRESULT status,const char* operation) { if (FAILED(status)) throw std::runtime_error(operation); }
struct Callback final : bgfx::CallbackI {
    void fatal(const char*,std::uint16_t,bgfx::Fatal::Enum,const char* message) override {
        std::fprintf(stderr,"bgfx fatal: %s\n",message); std::fflush(stderr); std::abort();
    }
    void traceVargs(const char*,std::uint16_t,const char* format,va_list args) override { std::vfprintf(stderr,format,args); }
    void profilerBegin(const char*,std::uint32_t,const char*,std::uint16_t) override {}
    void profilerBeginLiteral(const char*,std::uint32_t,const char*,std::uint16_t) override {}
    void profilerEnd() override {}
    std::uint32_t cacheReadSize(std::uint64_t) override { return 0; }
    bool cacheRead(std::uint64_t,void*,std::uint32_t) override { return false; }
    void cacheWrite(std::uint64_t,const void*,std::uint32_t) override {}
    void screenShot(const char*,std::uint32_t,std::uint32_t,std::uint32_t,bgfx::TextureFormat::Enum,const void*,std::uint32_t,bool) override {}
    void captureBegin(std::uint32_t,std::uint32_t,std::uint32_t,bgfx::TextureFormat::Enum,bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*,std::uint32_t) override {}
};
class BgfxAdapter final : public Adapter {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12InfoQueue> info;
    std::uint64_t nextFence=1;
    HANDLE event{};
    Callback callback;
    bool initialized{};
    std::string name;
    bgfx::TextureHandle target=BGFX_INVALID_HANDLE, uploadTarget=BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle framebuffer=BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program=BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout;
    std::uint16_t uploadWidth{},uploadHeight{};
    bool uploadHalf{};

    void wait() {
        const auto value=nextFence++;
        checked(queue->Signal(fence.Get(),value),"bgfx native queue signal failed");
        if (fence->GetCompletedValue()<value) {
            checked(fence->SetEventOnCompletion(value,event),"bgfx native fence event failed");
            if (WaitForSingleObject(event,60000)!=WAIT_OBJECT_0) throw std::runtime_error("bgfx native fence timed out");
        }
        checked(device->GetDeviceRemovedReason(),"bgfx D3D12 device removed");
        if (info) {
            for (UINT64 i=0;i<info->GetNumStoredMessages();++i) {
                SIZE_T size{}; checked(info->GetMessage(i,nullptr,&size),"bgfx info queue size failed");
                std::vector<std::byte> storage(size); auto message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                checked(info->GetMessage(i,message,&size),"bgfx info queue read failed");
                if (message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) {
                    std::fprintf(stderr,"bgfx D3D12 validation: %s\n",message->pDescription);
                    throw std::runtime_error("bgfx native D3D12 validation failed");
                }
            }
            info->ClearStoredMessages();
        }
    }
    std::vector<std::byte> download(bgfx::TextureHandle source,std::uint16_t w,std::uint16_t h,bool half) {
        // bgfx's public CPU-readback textures cannot simultaneously be sampled/render targets.
        auto readback=bgfx::createTexture2D(w,h,false,1,half?bgfx::TextureFormat::RGBA16F:bgfx::TextureFormat::RGBA8,
                                          BGFX_TEXTURE_READ_BACK|BGFX_TEXTURE_BLIT_DST);
        if (!bgfx::isValid(readback)) throw std::runtime_error("bgfx readback texture creation failed");
        std::vector<std::byte> bytes(static_cast<size_t>(w)*h*(half?8:4));
        bgfx::blit(1,bgfx::TextureRegion{readback},bgfx::TextureRegion{source});
        // Blit is a render command; read() is processed as a pre-render resource command.
        idle();
        const auto available=bgfx::read(bgfx::TextureRegion{readback},bytes.data());
        auto frame=bgfx::frame(); wait();
        for (unsigned i=0;frame<available && i<16;++i) { frame=bgfx::frame(); wait(); }
        if (frame<available) throw std::runtime_error("bgfx readback availability frame did not arrive");
        bgfx::destroy(readback); idle(); return bytes;
    }
public:
    explicit BgfxAdapter(bool validation) {
        if (validation) {
            ComPtr<ID3D12Debug> debug;
            checked(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"bgfx D3D12 debug layer unavailable");
            debug->EnableDebugLayer();
        }
        ComPtr<IDXGIFactory4> factory; checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"bgfx DXGI factory failed");
        DXGI_ADAPTER_DESC1 selected{}; ComPtr<IDXGIAdapter1> adapter;
        for (UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i) {
            checked(adapter->GetDesc1(&selected),"bgfx adapter description failed");
            if (!(selected.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)))) break;
            adapter.Reset();
        }
        if (!device) throw std::runtime_error("bgfx requires a hardware D3D12 adapter");
        auto luid=device->GetAdapterLuid();
        if (luid.HighPart!=selected.AdapterLuid.HighPart || luid.LowPart!=selected.AdapterLuid.LowPart) throw std::runtime_error("bgfx adapter LUID mismatch");
        const int length=WideCharToMultiByte(CP_UTF8,0,selected.Description,-1,nullptr,0,nullptr,nullptr);
        name.resize(length); WideCharToMultiByte(CP_UTF8,0,selected.Description,-1,name.data(),length,nullptr,nullptr); name.pop_back();
        D3D12_COMMAND_QUEUE_DESC desc{}; desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        checked(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)),"bgfx direct queue creation failed");
        checked(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"bgfx fence creation failed");
        if (validation) checked(device.As(&info),"bgfx native D3D12 info queue unavailable");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if (!event) throw std::runtime_error("bgfx fence event creation failed");
        try {
            bgfx::Init init; init.type=bgfx::RendererType::Direct3D12; init.fallback=false;
            init.vendorId=static_cast<std::uint16_t>(selected.VendorId); init.deviceId=static_cast<std::uint16_t>(selected.DeviceId);
            init.platformData.context=device.Get(); init.platformData.queue=queue.Get();
            init.swapChain.width=64; init.swapChain.height=64; init.swapChain.formatDepthStencil=bgfx::TextureFormat::Count;
            init.swapChain.maxFrameLatency=1; init.debug=false; init.profile=false; init.videoDecode=false;
            init.limits.maxEncoders=1; init.limits.numDrawCalls=100000; init.callback=&callback;
            if (!bgfx::init(init)) throw std::runtime_error("bgfx D3D12 initialization failed");
            initialized=true;
            if (bgfx::getRendererType()!=bgfx::RendererType::Direct3D12 || bgfx::getInternalData()->context!=device.Get()) throw std::runtime_error("bgfx did not use the supplied D3D12 device");
            ComPtr<ID3D12CommandQueue> actualQueue; UINT queueSize=sizeof(ID3D12CommandQueue*);
            checked(device->GetPrivateData(__uuidof(ID3D12CommandQueue),&queueSize,actualQueue.GetAddressOf()),"bgfx queue identity query failed");
            if (actualQueue.Get()!=queue.Get()) throw std::runtime_error("bgfx did not use the supplied D3D12 queue");
            const auto caps=bgfx::getCaps();
            if (caps->limits.maxDrawCalls<100000 || !(caps->formats[bgfx::TextureFormat::RGBA8]&BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
                || !(caps->formats[bgfx::TextureFormat::RGBA16F]&BGFX_CAPS_FORMAT_TEXTURE_2D))
                throw std::runtime_error("bgfx is missing the required draw capacity/texture formats");
            layout.begin(bgfx::RendererType::Direct3D12).add(bgfx::Attrib::Position,4,bgfx::AttribType::Float).end();
            idle();
            if (validation) std::fprintf(stderr,"bgfx native D3D12 validation enabled; ERROR/CORRUPTION fails the run\n");
        } catch (...) { if (initialized) bgfx::shutdown(); CloseHandle(event); event=nullptr; throw; }
    }
    ~BgfxAdapter() override {
        if (initialized) {
            if (bgfx::isValid(program)) bgfx::destroy(program);
            if (bgfx::isValid(framebuffer)) bgfx::destroy(framebuffer);
            if (bgfx::isValid(target)) bgfx::destroy(target);
            if (bgfx::isValid(uploadTarget)) bgfx::destroy(uploadTarget);
            bgfx::frame(); bgfx::shutdown();
            try { wait(); }
            catch (const std::exception& error) { std::fprintf(stderr,"bgfx teardown failed: %s\n",error.what()); std::abort(); }
        }
        if (event) CloseHandle(event);
    }
    std::string sourceFingerprint() const override { return bgfxBuildFingerprint(); }
    std::string deviceName() const override { return name; }
    std::string unsupportedReason(std::string_view scenario) const override {
        return scenario=="pipeline"?"Unsupported bgfx/pipeline: createProgram links shaders; the D3D12 PSO is created lazily on draw":"";
    }
    std::string allocationModel(std::string_view scenario) const override { return scenario=="buffers"?"bgfx_pooled_dynamic_vertex_buffers":"default_gpu_resources"; }
    void idle() override {
        // Single-thread bgfx frame executes resource commands inline. Reclamation spans frame slots.
        bgfx::frame(); bgfx::frame(); wait();
    }
    void* buffer(std::uint32_t bytes) override {
        auto handle=bgfx::createDynamicVertexBuffer(bytes/16,layout);
        if (!bgfx::isValid(handle)) throw std::runtime_error("bgfx dynamic vertex buffer allocation failed");
        return new bgfx::DynamicVertexBufferHandle(handle);
    }
    void releaseBuffer(void* ptr) override { std::unique_ptr<bgfx::DynamicVertexBufferHandle> handle(static_cast<bgfx::DynamicVertexBufferHandle*>(ptr)); bgfx::destroy(*handle); }
    void* texture(std::uint32_t w,std::uint32_t h,bool half) override {
        auto handle=bgfx::createTexture2D(static_cast<std::uint16_t>(w),static_cast<std::uint16_t>(h),false,1,half?bgfx::TextureFormat::RGBA16F:bgfx::TextureFormat::RGBA8);
        if (!bgfx::isValid(handle)) throw std::runtime_error("bgfx texture allocation failed");
        return new bgfx::TextureHandle(handle);
    }
    void releaseTexture(void* ptr) override { std::unique_ptr<bgfx::TextureHandle> handle(static_cast<bgfx::TextureHandle*>(ptr)); bgfx::destroy(*handle); }
    void prepareDraw(const Shaders& shaders) override {
        auto vsBytes=bgfxShaderContainer(shaders.vertex,false), psBytes=bgfxShaderContainer(shaders.fragment,true);
        auto vs=bgfx::createShader(bgfx::copy(vsBytes.data(),static_cast<std::uint32_t>(vsBytes.size())));
        auto ps=bgfx::createShader(bgfx::copy(psBytes.data(),static_cast<std::uint32_t>(psBytes.size())));
        if (!bgfx::isValid(vs) || !bgfx::isValid(ps)) throw std::runtime_error("bgfx wrapped shader creation failed");
        program=bgfx::createProgram(vs,ps,true);
        if (!bgfx::isValid(program)) throw std::runtime_error("bgfx program link failed");
        target=bgfx::createTexture2D(64,64,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT);
        framebuffer=bgfx::createFrameBuffer(1,&target,false);
        if (!bgfx::isValid(target) || !bgfx::isValid(framebuffer)) throw std::runtime_error("bgfx render target creation failed");
        idle();
    }
    DrawTimes draw(std::uint32_t count,bool instanced) override {
        if (count>100000) throw std::runtime_error("bgfx benchmark draw limit exceeded");
        auto start=Clock::now();
        bgfx::setViewFrameBuffer(0,framebuffer); bgfx::setViewRect(0,0,0,64,64);
        bgfx::setViewMode(0,bgfx::ViewMode::Sequential); bgfx::setViewClear(0,BGFX_CLEAR_COLOR,0x0000ffff);
        auto encode=[&](std::uint32_t instances) {
            bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A); bgfx::setVertexCount(3);
            if (instanced) bgfx::setInstanceCount(instances);
            bgfx::submit(0,program);
        };
        if (instanced) encode(count); else for (std::uint32_t i=0;i<count;++i) encode(1);
        auto recorded=Clock::now(); bgfx::frame(); auto submitted=Clock::now(); wait(); auto end=Clock::now();
        // With BGFX_CONFIG_MULTITHREADED=0 public draw stats are current after frame().
        // Non-indexed primitive stats are not used as an instance-count correctness oracle.
        if (bgfx::getStats()->numDraw!=(instanced?1:count)) throw std::runtime_error("bgfx dropped draw submissions");
        return {ms(start,recorded),ms(recorded,submitted),ms(submitted,end),ms(start,end),std::nullopt};
    }
    bool verifyDraw() override {
        auto pixels=download(target,64,64,false); const size_t center=(32*64+32)*4;
        return pixels[center]==std::byte{255} && pixels[center+2]==std::byte{0} && pixels[0]==std::byte{0} && pixels[2]==std::byte{255};
    }
    PipelineTimes recreatePipeline() override { throw std::runtime_error(unsupportedReason("pipeline")); }
    void prepareUpload(std::uint32_t w,std::uint32_t h,bool half) override {
        uploadWidth=static_cast<std::uint16_t>(w); uploadHeight=static_cast<std::uint16_t>(h); uploadHalf=half;
        uploadTarget=bgfx::createTexture2D(uploadWidth,uploadHeight,false,1,half?bgfx::TextureFormat::RGBA16F:bgfx::TextureFormat::RGBA8);
        if (!bgfx::isValid(uploadTarget)) throw std::runtime_error("bgfx upload texture creation failed");
        idle();
    }
    void upload(std::span<const std::byte> pixels) override {
        // The harness keeps pixels alive and immutable through the complete campaign/readback.
        // Let bgfx stage from that persistent source without a redundant caller-side ownership copy.
        bgfx::updateTexture2D(uploadTarget,0,0,0,0,uploadWidth,uploadHeight,bgfx::makeRef(pixels.data(),static_cast<std::uint32_t>(pixels.size())));
        bgfx::frame(); wait();
    }
    bool verifyUpload(std::span<const std::byte> pixels) override {
        auto actual=download(uploadTarget,uploadWidth,uploadHeight,uploadHalf); return std::equal(pixels.begin(),pixels.end(),actual.begin(),actual.end());
    }
};
}
std::unique_ptr<Adapter> bgfxAdapter(bool validation) { return std::make_unique<BgfxAdapter>(validation); }
}
