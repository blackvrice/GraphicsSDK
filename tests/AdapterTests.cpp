#include <graphics/Graphics.hpp>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#if defined(_WIN32) && defined(GFX_TEST_D3D12)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <bit>
#endif

namespace {
unsigned assertions{},images{};
void check(bool value,const char* text) { ++assertions; if (!value) throw std::runtime_error(text); }
template<class T> T take(gfx::Result<T> value) {
    if (!value) throw std::runtime_error(value.error().message+" native="+std::to_string(value.error().nativeCode));
    return std::move(*value);
}
void take(gfx::Result<void> value) { if (!value) throw std::runtime_error(value.error().message); }
template<class T> void reject(gfx::Result<T> value,gfx::ErrorCode code,const char* text) {
    check(!value && value.error().code==code,text);
}
void cpu() {
    if (!gfx::backendCompiled(gfx::Backend::Vulkan))
        reject(gfx::enumerateAdapters(gfx::Backend::Vulkan),gfx::ErrorCode::Unsupported,"Uncompiled enumeration must fail");
    reject(gfx::enumerateAdapters(gfx::Backend::Metal),gfx::ErrorCode::Unsupported,"Uncompiled Metal enumeration must fail");
    gfx::DeviceDesc d;d.adapter=gfx::AdapterId{gfx::Backend::Metal,0};
    reject(gfx::createDevice(d),gfx::ErrorCode::InvalidArgument,"Reject cross-backend identity before dispatch");
    if (!gfx::backendCompiled(gfx::Backend::D3D12))
        reject(gfx::enumerateAdapters(),gfx::ErrorCode::Unsupported,"No-backend enumeration is unsupported");
    check(gfx::AdapterId{gfx::Backend::D3D12,1}!=gfx::AdapterId{gfx::Backend::Metal,1},"Identity includes backend");
}
#if defined(_WIN32) && defined(GFX_TEST_D3D12)
using Microsoft::WRL::ComPtr;
void native(HRESULT hr,const char* text) { check(SUCCEEDED(hr),text); }
gfx::AdapterId identity(LUID value) {
    return {gfx::Backend::D3D12,(static_cast<std::uint64_t>(static_cast<std::uint32_t>(value.HighPart))<<32)|value.LowPart};
}
LUID luid(gfx::AdapterId value) {
    return {static_cast<DWORD>(value.value),std::bit_cast<LONG>(static_cast<DWORD>(value.value>>32))};
}
void image(gfx::Device& device,unsigned color) {
    constexpr std::string_view source=R"(
float4 VS(uint id:SV_VertexID):SV_Position {
float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)}; return float4(p[id],0,1); }
float4 PS():SV_Target0 { return float4(1,0,1,1); }
)";
    auto vertex=take(gfx::compileHlsl(source,"VS",gfx::ShaderStage::Vertex));
    auto fragment=take(gfx::compileHlsl(source,"PS",gfx::ShaderStage::Fragment));
    auto pipeline=take(device.createGraphicsPipeline({vertex,fragment}));
    auto texture=take(device.createTexture({31,17,gfx::Format::RGBA8UNorm,true}));
    const auto f=texture->footprint();auto readback=take(device.createBuffer({f.size,gfx::Memory::Readback}));
    auto cmd=take(device.createCommandList());take(cmd->begin());
    take(cmd->transition(texture,gfx::ResourceState::RenderTarget));
    take(cmd->beginRenderPass(texture,{0,color%2?1.f:0.f,0,1}));take(cmd->setPipeline(pipeline));
    take(cmd->setScissor(3,2,23,11));take(cmd->draw(3));take(cmd->endRenderPass());
    take(cmd->transition(texture,gfx::ResourceState::CopySource));take(cmd->copyTextureToBuffer(texture,readback));take(cmd->end());
    take(device.queue().wait(take(device.queue().submit(*cmd))));
    const auto bytes=take(readback->read(0,f.size));
    for (unsigned y=0;y<17;++y) for (unsigned x=0;x<31;++x) {
        const bool inside=x>=3 && x<26 && y>=2 && y<13;
        const std::array<unsigned,4> expected=inside?std::array<unsigned,4>{255,0,255,255}:std::array<unsigned,4>{0,color%2?255u:0u,0,255};
        for (unsigned c=0;c<4;++c)
            check(std::to_integer<unsigned>(bytes[y*f.rowPitch+x*4+c])==expected[c],"Selected adapter full-image mismatch");
    }
    ++images;
}
void exercise(bool validation) {
    const auto adapters=take(gfx::enumerateAdapters());
    check(!adapters.empty(),"Adapter list must not be empty on this native test host");
    std::unordered_set<std::uint64_t> seen;
    ComPtr<IDXGIFactory6> factory;native(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Independent factory");
    for (const auto& info:adapters) {
        check(info.id.backend==gfx::Backend::D3D12 && !info.name.empty() && seen.insert(info.id.value).second,"Unique backend-scoped identities");
        ComPtr<IDXGIAdapter1> adapter;native(factory->EnumAdapterByLuid(luid(info.id),IID_PPV_ARGS(&adapter)),"Independent native adapter lookup");
        DXGI_ADAPTER_DESC1 description{};native(adapter->GetDesc1(&description),"Independent adapter metadata");
        check(identity(description.AdapterLuid)==info.id && info.vendorId==description.VendorId && info.deviceId==description.DeviceId
              && info.softwareAdapter==((description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)!=0)
              && info.dedicatedVideoMemory==description.DedicatedVideoMemory && info.sharedSystemMemory==description.SharedSystemMemory,
              "Metadata matches independent DXGI snapshot");
        gfx::DeviceDesc desc{gfx::Backend::D3D12,validation,info.softwareAdapter,true,info.id};
        auto mismatch=desc;mismatch.validation=false;mismatch.softwareAdapter=!info.softwareAdapter;
        reject(gfx::createDevice(mismatch),gfx::ErrorCode::InvalidArgument,"Explicit selection rejects software-kind mismatch");
        auto device=take(gfx::createDevice(desc));
        check(device->caps().adapterId==info.id && device->caps().adapterName==info.name && device->caps().softwareAdapter==info.softwareAdapter,
              "Selected native identity and metadata match request");
        image(*device,0);
        const auto again=take(gfx::enumerateAdapters());
        check(again.size()==adapters.size(),"Healthy enumeration remains a fresh complete snapshot");
        take(device->checkStatus());
        if (validation) check(take(device->validationErrorCount())==0,"Selected device validation before native removal");
        // Independent removal of the requested native singleton proves the SDK really uses it.
        ComPtr<ID3D12Device> owner;native(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&owner)),"Independent device owner");
        check(identity(owner->GetAdapterLuid())==info.id,"Independent native device LUID");
        ComPtr<ID3D12Device5> remover;native(owner.As(&remover),"Independent removal interface");
        remover->RemoveDevice();remover.Reset();
        const auto status=device->checkStatus();
        check(!status && status.error().code==gfx::ErrorCode::DeviceLost && status.error().nativeCode==owner->GetDeviceRemovedReason(),
              "Removing selected native adapter loses this exact SDK device");
        reject(device->recover(),gfx::ErrorCode::DeviceLost,"Exact recovery stays blocked by the selected removed singleton owner");
        owner.Reset();
        auto fresh=take(device->recover());
        check(fresh->caps().adapterId==info.id && fresh->caps().softwareAdapter==info.softwareAdapter,"Recovery preserves exact selection");
        image(*fresh,1);take(fresh->queue().waitIdle());
        if (validation) check(take(fresh->validationErrorCount())==0,"Recovered selected device validation");
        std::cout<<"adapter_id="<<info.id.value<<" software="<<info.softwareAdapter<<" name="<<info.name
                 <<" native_reason="<<status.error().nativeCode<<" before_after_images=2 exact_recovery_passed\n";
    }
    std::uint64_t missing=std::numeric_limits<std::uint64_t>::max();while(seen.contains(missing))--missing;
    gfx::DeviceDesc absent;absent.adapter=gfx::AdapterId{gfx::Backend::D3D12,missing};
    reject(gfx::createDevice(absent),gfx::ErrorCode::Unsupported,"Missing identity must not silently fall back");
    std::cout<<"selected_adapters="<<adapters.size()<<" missing_identity_rejected\n";
}
#endif
}
int main(int argc,char** argv) try {
    const std::string_view mode=argc>1?argv[1]:"--cpu";
    if (mode=="--cpu") cpu();
#if defined(_WIN32) && defined(GFX_TEST_D3D12)
    else if (mode=="--gpu" || mode=="--validation") exercise(mode=="--validation");
#endif
    else throw std::runtime_error("Unknown/unavailable adapter test mode");
    std::cout<<"PASS: "<<assertions<<" assertions, "<<images<<" full images\n";return 0;
} catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
