#include "Contract.hpp"
#include <Windows.h>
#include "../../benchmarks/common/Adapter.hpp"
#include "../../benchmarks/vulkan_upload/Identity.hpp"
#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <span>
#ifndef NDEBUG
#error Release required
#endif
namespace {
using namespace loaderfault;
unsigned checks{},verified{},frames{};std::uint64_t bytes{};
void check(bool x,const char*m){++checks;if(!x)throw std::runtime_error(m);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message+" native="+std::to_string(r.error().nativeCode));return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
struct Client {
    HMODULE module{};using Arm=int(__cdecl*)(unsigned,int,unsigned);using Read=int(__cdecl*)(Snapshot*,unsigned);Arm arm{};Read read{};
    Client(){
        wchar_t expected[32768]{};auto n=GetEnvironmentVariableW(L"GFX_EXPECT_LOADER_LAYER",expected,32768);
        check(n&&n<32768&&std::filesystem::path(expected).is_absolute(),"Absolute expected test layer required");
        module=LoadLibraryExW(expected,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);check(module!=nullptr,"Preload test DLL");
        wchar_t actual[32768]{};n=GetModuleFileNameW(module,actual,32768);check(n&&n<32768&&std::filesystem::equivalent(actual,expected),"Loaded test DLL path");
        arm=reinterpret_cast<Arm>(GetProcAddress(module,"gfxLoaderArm"));read=reinterpret_cast<Read>(GetProcAddress(module,"gfxLoaderSnapshot"));check(arm&&read,"Test controller exports");
    }
    ~Client(){if(module)FreeLibrary(module);}
    Snapshot snapshot(){Snapshot s;check(read(&s,sizeof(s))!=0,"Snapshot ABI");return s;}
};
void emit(const Snapshot&s){
    std::cout<<'{';bool comma{};
    for(auto [name,p]:{std::pair{"attempts",s.attempts},{"created",s.created},{"destroyed",s.destroyed},{"live",s.live}}){
        if(comma)std::cout<<',';comma=true;std::cout<<'"'<<name<<"\":[";for(unsigned k=0;k<Count;++k){if(k)std::cout<<',';std::cout<<p[k];}std::cout<<']';
    }
    std::cout<<",\"invalid\":"<<s.invalid<<",\"errors\":"<<s.errors<<",\"injections\":"<<s.injections<<",\"pending\":"<<s.pending<<'}';
}
std::vector<std::byte> payload(unsigned seed){std::vector<std::byte> p(4096);for(unsigned i=0;i<p.size();++i)p[i]=std::byte((i*37+seed*53)&255);return p;}
void copyCheck(gfx::Device&d,const gfx::BufferPtr&b,unsigned seed,bool initialize){
    auto expected=payload(seed);gfx::BufferPtr up;
    if(initialize){up=take(d.createBuffer({4096,gfx::Memory::Upload}));take(up->write(0,expected));}
    auto down=take(d.createBuffer({4096,gfx::Memory::Readback}));auto c=take(d.createCommandList());take(c->begin());
    if(up){take(c->transition(b,gfx::ResourceState::CopyDestination));take(c->copyBuffer(up,b,4096));}
    take(c->transition(b,gfx::ResourceState::CopySource));take(c->copyBuffer(b,down,4096));take(c->end());take(d.queue().wait(take(d.queue().submit(*c))));
    check(take(down->read(0,4096))==expected,"Full buffer data retained");++verified;bytes+=4096;
}
bool empty(const gfx::ResourceStatistics&s){return !s.trackedObjects&&!s.uniqueResources&&!s.resourceAllocationBytes&&!s.pendingSubmissions&&!s.pendingObjectReferences;}
struct Window {
    HWND handle{};
    Window(){RECT r{0,0,321,197};check(AdjustWindowRectEx(&r,WS_OVERLAPPEDWINDOW,FALSE,0)!=0,"Window extent");handle=CreateWindowExW(0,L"STATIC",L"GraphicsSDK loader retry",WS_OVERLAPPEDWINDOW,20,20,r.right-r.left,r.bottom-r.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(handle!=nullptr,"Hidden native HWND");}
    ~Window(){if(handle)DestroyWindow(handle);}
};
void frame(gfx::Device&d,gfx::SwapChain&s){
    using S=gfx::ResourceState;auto t=take(s.acquire());auto fp=t->footprint();auto td=t->description();
    auto down=take(d.createBuffer({fp.size,gfx::Memory::Readback}));auto up=take(d.createBuffer({fp.size,gfx::Memory::Upload}));
    take(up->write(0,std::vector<std::byte>(static_cast<std::size_t>(fp.size),std::byte{0xab})));
    auto c=take(d.createCommandList());take(c->begin());take(c->transition(t,S::RenderTarget));take(c->beginRenderPass(t,{.25f,.5f,.75f,1}));take(c->endRenderPass());
    take(c->copyBuffer(up,down,fp.size));take(c->transition(t,S::CopySource));take(c->copyTextureToBuffer(t,down));take(c->transition(t,S::Present));take(c->end());
    auto fence=take(d.queue().submit(*c));take(s.present());take(d.queue().wait(fence));auto data=take(down->read(0,fp.size));
    std::array<float,4> color{.25f,.5f,.75f,1};bool pixels=true,padding=true;
    for(unsigned y=0;y<td.height;++y){auto row=std::size_t(y)*fp.rowPitch;
        for(unsigned x=0;x<td.width;++x)for(unsigned k=0;k<4;++k)pixels&=std::abs(std::to_integer<unsigned>(data[row+x*4+k])/255.f-color[k])<=1.1f/255;
        padding&=std::all_of(data.begin()+row+fp.rowBytes,data.begin()+row+fp.rowPitch,[](auto b){return b==std::byte{0xab};});
    }
    check(pixels&&padding,"All retry frame pixels and padding");++frames;bytes+=std::uint64_t(td.width)*td.height*4;
}
std::string validationLayer(bool enabled){
    auto m=GetModuleHandleW(L"VkLayer_khronos_validation.dll");if(!enabled){check(!m,"Validation unexpectedly loaded");return{};}
    wchar_t actual[32768]{},expected[32768]{};auto n=m?GetModuleFileNameW(m,actual,32768):0;auto e=GetEnvironmentVariableW(L"GFX_EXPECT_VULKAN_LAYER",expected,32768);
    check(n&&n<32768&&e&&e<32768&&std::filesystem::equivalent(actual,expected),"Pinned validation DLL");return std::filesystem::path(actual).generic_string();
}
}
int main(int argc,char**argv)try{
    using bench::quote;
    if(argc==2&&std::string_view(argv[1])=="--probe"){
        auto a=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));std::cout<<"{\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"adapters\":[";bool comma{};
        for(auto&x:a)if(!x.softwareAdapter){if(comma)std::cout<<',';comma=true;std::cout<<quote(x.name);}
        std::cout<<"],\"presentation\":[";comma=false;
        for(auto&x:a)if(!x.softwareAdapter){gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=x.id;auto d=take(gfx::createDevice(desc));if(comma)std::cout<<',';comma=true;std::cout<<(d->caps().swapChain?"true":"false");}
        std::cout<<"]}\n";return 0;
    }
    if(argc!=6)throw std::runtime_error("Expected KIND host-or-device GPU validation-0-or-1 poison-0-or-1");
    unsigned kind=Count;for(unsigned k=0;k<Count;++k)if(names[k]==std::string_view(argv[1]))kind=k;
    auto error=std::string_view(argv[2]);int code=error=="host"?-1:error=="device"?-2:0;
    if(kind==Count||!code||(std::string_view(argv[4])!="0"&&std::string_view(argv[4])!="1")||(std::string_view(argv[5])!="0"&&std::string_view(argv[5])!="1"))throw std::runtime_error("Invalid CLI");
    bool validation=std::string_view(argv[4])=="1";unsigned poison=std::string_view(argv[5])=="1"?1u:0u;Plan input;
    if(!input.arm(kind,code,poison)||(kind==Messenger&&!validation))throw std::runtime_error("Invalid fault profile");
    auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));auto a=std::find_if(adapters.begin(),adapters.end(),[&](auto&x){return x.name==argv[3]&&!x.softwareAdapter;});
    if(a==adapters.end()||std::count_if(adapters.begin(),adapters.end(),[&](auto&x){return x.name==argv[3];})!=1)throw std::runtime_error("Unique exact GPU required");
    auto identity=upload::identify(a->name);Client fault;
    // The first successful SDK device proves the explicit layer was actually loaded.
    gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=a->id;desc.validation=validation;
    auto d=take(gfx::createDevice(desc));if(kind==Surface)check(d->caps().swapChain,"Real Win32 presentation capability");auto layer=validationLayer(validation);
    auto initial=fault.snapshot();check(initial.live[Instance]==1&&initial.live[Device]==1&&initial.live[Messenger]==unsigned(validation),"SDK loader chain observed");
    struct Cycle{Snapshot before,failed,retry,clean;};std::array<Cycle,3> cycles;
    for(unsigned i=0;i<cycles.size();++i){
        auto guard=take(d->createBuffer({4096}));copyCheck(*d,guard,i+1,true);auto stats=take(d->resourceStatistics());Window w;auto&c=cycles[i];c.before=fault.snapshot();
        check(fault.arm(kind,code,poison)!=0,"Arm one error");
        if(kind==Surface){auto r=d->createSwapChain({gfx::SurfaceKind::Win32,w.handle},{321,197,2,true});check(!r&&r.error().nativeCode==code&&r.error().code==gfx::ErrorCode::OutOfMemory,"Surface error preserved");}
        else {auto r=gfx::createDevice(desc);check(!r&&r.error().nativeCode==code&&r.error().code==gfx::ErrorCode::OutOfMemory,"Initialization error preserved");}
        c.failed=fault.snapshot();check(!c.failed.pending&&c.failed.injections==c.before.injections+1,"Exactly one fault consumed");
        for(unsigned k=0;k<Count;++k)check(c.failed.live[k]==c.before.live[k],"Failed creation preserves native live ownership");
        auto after=take(d->resourceStatistics());check(after.trackedObjects==stats.trackedObjects&&after.uniqueResources==stats.uniqueResources&&after.resourceAllocationBytes==stats.resourceAllocationBytes&&after.pendingSubmissions==stats.pendingSubmissions&&after.pendingObjectReferences==stats.pendingObjectReferences,"Existing SDK resources retained");
        copyCheck(*d,guard,i+1,false);
        if(kind==Surface){auto s=take(d->createSwapChain({gfx::SurfaceKind::Win32,w.handle},{321,197,2,true}));frame(*d,*s);c.retry=fault.snapshot();}
        else {auto retry=take(gfx::createDevice(desc));auto b=take(retry->createBuffer({4096}));copyCheck(*retry,b,i+11,true);if(validation)check(take(retry->validationErrorCount())==0,"Retry validation errors");c.retry=fault.snapshot();}
        c.clean=fault.snapshot();for(unsigned k=0;k<Count;++k)check(c.clean.live[k]==c.before.live[k],"Retry objects explicitly reclaimed");
        copyCheck(*d,guard,i+1,false);guard.reset();take(d->queue().waitIdle());check(empty(take(d->resourceStatistics())),"Public resource counts empty");take(d->checkStatus());
    }
    if(validation)check(take(d->validationErrorCount())==0,"Guard validation errors");d.reset();auto final=fault.snapshot();
    for(unsigned k=0;k<Count;++k)check(final.live[k]==0&&final.created[k]==final.destroyed[k],"Final native object conservation");check(!final.errors&&!final.pending&&final.injections==3,"Final trace contract");
    std::cout<<"{\"schema_version\":1,\"contract\":\"vulkan_loader_fault_v1\",\"kind\":"<<quote(names[kind])<<",\"result\":"<<code<<",\"poison\":"<<poison<<",\"validation\":"<<(validation?"true":"false")<<",\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"source_fingerprint\":"<<quote(GFX_LOADER_FINGERPRINT)<<",\"device_name\":"<<quote(a->name)<<",\"device_uuid\":"<<quote(identity.uuid)<<",\"driver_uuid\":"<<quote(identity.driverUuid)<<",\"loaded_validation_layer\":"<<quote(layer)<<",\"environment\":"<<bench::environment(a->name)<<",\"assertions\":"<<checks<<",\"verified_buffers\":"<<verified<<",\"frames\":"<<frames<<",\"verified_bytes\":"<<bytes<<",\"cycles\":[";
    for(unsigned i=0;i<cycles.size();++i){if(i)std::cout<<',';auto&c=cycles[i];std::cout<<"{\"before\":";emit(c.before);std::cout<<",\"failed\":";emit(c.failed);std::cout<<",\"retry\":";emit(c.retry);std::cout<<",\"clean\":";emit(c.clean);std::cout<<'}';}
    std::cout<<"],\"final\":";emit(final);std::cout<<"}\n";return final.invalid?2:0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
