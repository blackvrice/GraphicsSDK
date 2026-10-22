#include <graphics/Graphics.hpp>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <set>
#include <thread>

namespace {
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
using S=gfx::ResourceState;
constexpr std::uint64_t MiB=1024*1024;
constexpr unsigned side=64, ring=8, copyBytes=256*1024;
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void hr(HRESULT value){check(SUCCEEDED(value),"OS telemetry query failed");}
std::uint64_t identity(LUID luid){return (std::uint64_t(static_cast<std::uint32_t>(luid.HighPart))<<32)|luid.LowPart;}
std::uint64_t decimal(std::string_view text){std::uint64_t value{};auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);check(error==std::errc{}&&end==text.data()+text.size(),"Expected unsigned decimal integer");return value;}
PROCESS_MEMORY_COUNTERS_EX processMemory(){PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof(p);check(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p))!=0,"Private commit query");return p;}
std::string utf8(const wchar_t* value){int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);check(n>0,"Adapter name conversion");std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,value,-1,out.data(),n,nullptr,nullptr);out.pop_back();return out;}
std::string quote(std::string_view s){std::string out="\"";for(unsigned char c:s){if(c=='\"'||c=='\\')out+='\\';check(c>=32,"Unexpected JSON control character");out+=char(c);}return out+'\"';}
struct Observer {
    ComPtr<IDXGIAdapter3> adapter;
    DXGI_ADAPTER_DESC1 desc{};
    explicit Observer(const gfx::DeviceCaps& caps){
        ComPtr<IDXGIFactory4> factory;hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        check(caps.adapterId.backend==gfx::Backend::D3D12,"Telemetry requires D3D12 identity");
        const LUID luid{static_cast<DWORD>(caps.adapterId.value),std::bit_cast<LONG>(static_cast<std::uint32_t>(caps.adapterId.value>>32))};
        hr(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter)));hr(adapter->GetDesc1(&desc));
        check(identity(desc.AdapterLuid)==caps.adapterId.value&&utf8(desc.Description)==caps.adapterName&&bool(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)==caps.softwareAdapter,"Telemetry adapter identity mismatch");
    }
    void write(std::ostream& out){
        const auto p=processMemory();
        DWORD handles{};check(GetProcessHandleCount(GetCurrentProcess(),&handles)!=0,"Handle count query");
        DXGI_QUERY_VIDEO_MEMORY_INFO local{},nonlocal{};
        const auto a=adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local),b=adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&nonlocal);
        out<<",\"private_bytes\":"<<p.PrivateUsage<<",\"working_set_bytes\":"<<p.WorkingSetSize<<",\"handles\":"<<handles
           <<",\"gpu_local_hresult\":"<<static_cast<std::int32_t>(a)<<",\"gpu_nonlocal_hresult\":"<<static_cast<std::int32_t>(b);
        out<<",\"gpu_local_bytes\":"<<(SUCCEEDED(a)?std::to_string(local.CurrentUsage):"null")<<",\"gpu_nonlocal_bytes\":"<<(SUCCEEDED(b)?std::to_string(nonlocal.CurrentUsage):"null")
           <<",\"gpu_local_budget\":"<<(SUCCEEDED(a)?std::to_string(local.Budget):"null")<<",\"gpu_nonlocal_budget\":"<<(SUCCEEDED(b)?std::to_string(nonlocal.Budget):"null");
    }
};
void writeStats(std::ostream& out,const gfx::ResourceStatistics& s){
    out<<",\"objects\":"<<s.trackedObjects<<",\"resources\":"<<s.uniqueResources<<",\"allocation_bytes\":"<<s.resourceAllocationBytes<<",\"pending\":"<<s.pendingSubmissions<<",\"pending_refs\":"<<s.pendingObjectReferences;
}
struct Frame {gfx::BufferPtr a,b,z,copy;gfx::TextureFootprint f;gfx::Fence fence;unsigned index;};
gfx::GraphicsPipelineDesc pipelineDesc(){
    constexpr auto code=R"(
    cbuffer Params:register(b0){float4 color;}
    Texture2D<float4> image:register(t0);
    float4 VS(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*2-1,.5,1);}
    struct Out{float4 a:SV_Target0;float4 b:SV_Target1;};
    Out PS(float4 p:SV_Position){Out o;o.a=image.Load(int3(p.xy,0))*color;o.b=float4(1-o.a.rgb,1);return o;})";
    gfx::GraphicsPipelineDesc p;p.vertex=take(gfx::compileHlsl(code,"VS",gfx::ShaderStage::Vertex));p.fragment=take(gfx::compileHlsl(code,"PS",gfx::ShaderStage::Fragment));
    p.bindingLayout={{{gfx::BindingType::Texture2D,0},{gfx::BindingType::ConstantBuffer,0}}, {}};
    p.renderTargets=gfx::RenderTargetLayout{{gfx::Format::RGBA8UNorm,gfx::Format::RGBA8UNorm},gfx::Format::D32Float};p.depthState={true,true,gfx::CompareOp::Less};return p;
}
Frame submit(gfx::Device& d,const gfx::PipelinePtr& pipeline,unsigned index){
    auto a=take(d.createTexture({side,side,gfx::Format::RGBA8UNorm,true})),b=take(d.createTexture({side,side,gfx::Format::RGBA8UNorm,true})),z=take(d.createTexture({side,side,gfx::Format::D32Float,false,true}));
    auto image=take(d.createTexture({side,side}));auto f=a->footprint();
    auto upload=take(d.createBuffer({f.size,gfx::Memory::Upload}));std::vector<std::byte> white(f.size,std::byte{255});take(upload->write(0,white));
    auto cb=take(d.createBuffer({512,gfx::Memory::Upload}));const std::array<float,4> color{index%2?0.f:1.f,index%2?1.f:0.f,0,1};take(cb->write(0,std::as_bytes(std::span{color})));take(cb->write(256,std::as_bytes(std::span{color})));
    auto input=take(d.createBuffer({copyBytes,gfx::Memory::Upload})),device=take(d.createBuffer({copyBytes}));std::vector<std::byte> pattern(copyBytes,std::byte(index%251));take(input->write(0,pattern));
    Frame r{take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({copyBytes,gfx::Memory::Readback})),f,{},index};
    auto c=take(d.createCommandList());take(c->begin());take(c->transition(image,S::CopyDestination));take(c->copyBufferToTexture(upload,image));take(c->transition(image,S::ShaderRead));take(c->transition(cb,S::ConstantBuffer));
    take(c->transition(a,S::RenderTarget));take(c->transition(b,S::RenderTarget));take(c->transition(z,S::DepthWrite));take(c->beginRenderPass({{{a},{b}},{z}}));take(c->setPipeline(pipeline));
    take(c->bindResources({{{0,0,image}},{{1,0,cb,0,256}}}));take(c->setScissor(0,0,side/2,side));take(c->draw(3));
    take(c->bindResources({{{0,0,image}},{{1,0,cb,256,256}}}));take(c->setScissor(side/2,0,side/2,side));take(c->draw(3));take(c->endRenderPass());
    take(c->transition(a,S::CopySource));take(c->copyTextureToBuffer(a,r.a));take(c->transition(b,S::CopySource));take(c->copyTextureToBuffer(b,r.b));take(c->transition(z,S::CopySource));take(c->copyTextureToBuffer(z,r.z));
    take(c->transition(device,S::CopyDestination));take(c->copyBuffer(input,device,copyBytes));take(c->transition(device,S::CopySource));take(c->copyBuffer(device,r.copy,copyBytes));
    take(c->writeTimestamp(0));take(c->writeTimestamp(1));take(c->resolveTimestamps());take(c->end());r.fence=take(d.queue().submit(*c));
    // Drop commands and every non-readback wrapper while native submission ownership remains.
    return r;
}
void verify(const Frame& r){
    const auto a=take(r.a->read(0,r.f.size)),b=take(r.b->read(0,r.f.size)),z=take(r.z->read(0,r.f.size)),copy=take(r.copy->read(0,copyBytes));
    for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){auto offset=y*r.f.rowPitch+x*4;for(unsigned k=0;k<4;++k){unsigned expected=k==3||k==r.index%2?255:0;check(a[offset+k]==std::byte(expected),"Full MRT0 CPU reference mismatch");check(b[offset+k]==std::byte(k==3?255:255-expected),"Full MRT1 CPU reference mismatch");}float depth{};std::memcpy(&depth,z.data()+offset,4);check(depth==.5f,"Full D32 CPU reference mismatch");}
    for(auto value:copy)check(value==std::byte(r.index%251),"Full copied buffer reference mismatch");
}
void statisticsContracts(gfx::Device& d){
    auto zero=[&]{const auto s=take(d.resourceStatistics());check(s.trackedObjects==0&&s.uniqueResources==0&&s.resourceAllocationBytes==0&&s.pendingSubmissions==0&&s.pendingObjectReferences==0,"Empty statistics contract");};
    zero();
    auto a=take(d.createBuffer({256,gfx::Memory::Upload})),b=take(d.createBuffer({256})),r=take(d.createBuffer({256,gfx::Memory::Readback}));
    auto alias=b;auto s=take(d.resourceStatistics());check(s.uniqueResources==3&&s.trackedObjects==3&&s.resourceAllocationBytes==3*65536,"Unique committed allocation accounting");alias.reset();
    auto c=take(d.createCommandList());take(c->begin());take(c->transition(b,S::CopyDestination));take(c->copyBuffer(a,b,256));take(c->transition(b,S::CopySource));take(c->copyBuffer(b,r,256));take(c->end());
    s=take(d.resourceStatistics());check(s.uniqueResources==4&&s.trackedObjects==4&&s.resourceAllocationBytes==4*65536,"Recorded command resources must be deduplicated including timestamp readback");
    a.reset();b.reset();r.reset();s=take(d.resourceStatistics());check(s.trackedObjects==1&&s.uniqueResources==4,"Unsubmitted command native ownership outlives wrappers");
    auto fence=take(d.queue().submit(*c));c.reset();take(d.queue().wait(fence));zero();
    for(unsigned i=0;i<600;++i){auto temporary=take(d.createBuffer({256}));}zero();
    std::cout<<"PASS: statistics unique resources, aliases, recorded/native ownership, fence reclamation and weak registry\n";
}
}
int main(int argc,char** argv)try{
    std::string mode="gpu",output;std::uint64_t seconds=0,warmup=30,interval=5,maxGrowthMiB=512;bool replace=false;std::optional<gfx::AdapterId> selected;std::set<std::string_view> seen;
    for(int i=1;i<argc;++i){std::string_view arg=argv[i];check(seen.insert(arg).second,"Duplicate stress option");check(i+1<argc,"Every option requires a value");const std::string_view value=argv[++i];if(arg=="--mode")mode=value;else if(arg=="--output")output=value;else if(arg=="--seconds")seconds=decimal(value);else if(arg=="--warmup")warmup=decimal(value);else if(arg=="--interval")interval=decimal(value);else if(arg=="--adapter")selected=gfx::AdapterId{gfx::Backend::D3D12,decimal(value)};else if(arg=="--max-private-growth-mib")maxGrowthMiB=decimal(value);else if(arg=="--replace"&&value=="1")replace=true;else throw std::runtime_error("Unknown option");}
    check((mode=="gpu"||mode=="validation"||mode=="warp")&&!output.empty()&&seconds>0&&seconds<=86400&&warmup<=3600&&interval>0&&interval<=60,"Invalid stress configuration");
    check(maxGrowthMiB>=1&&maxGrowthMiB<=4096,"Private growth guard must be 1..4096 MiB");
    auto d=take(gfx::createDevice({gfx::Backend::D3D12,mode=="validation",mode=="warp",false,selected}));Observer observer(d->caps());statisticsContracts(*d);
    std::ifstream exists(output);check(replace||!exists.good(),"Refusing to overwrite stress evidence");exists.close();std::ofstream out(output);check(out.good(),"Open stress evidence");out.exceptions(std::ios::badbit|std::ios::failbit);out<<std::setprecision(12);const auto start=Clock::now();
    auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-start).count();};
    const auto baselinePrivate=processMemory().PrivateUsage,guardBytes=maxGrowthMiB*MiB;
    out<<"{\"type\":\"header\",\"schema\":2,\"mode\":"<<quote(mode)<<",\"adapter\":"<<quote(d->caps().adapterName)<<",\"adapter_id\":"<<d->caps().adapterId.value<<",\"observer_adapter_id\":"<<identity(observer.desc.AdapterLuid)<<",\"requested_adapter_id\":"<<(selected?std::to_string(selected->value):"null")<<",\"software\":"<<(d->caps().softwareAdapter?"true":"false")<<",\"gpu_validation\":"<<(mode=="validation"?"true":"false")<<",\"gpu_state_tracking\":"<<(mode=="validation"?"true":"false")<<",\"private_growth_limit_bytes\":"<<guardBytes<<",\"guard_baseline_private_bytes\":"<<baselinePrivate<<",\"luid_high\":"<<observer.desc.AdapterLuid.HighPart<<",\"luid_low\":"<<observer.desc.AdapterLuid.LowPart<<",\"sdk_sha256\":"<<quote(gfx::buildFingerprint())<<",\"seconds\":"<<seconds<<",\"warmup\":"<<warmup<<",\"interval\":"<<interval<<",\"ring\":"<<ring<<",\"side\":"<<side<<",\"copy_bytes\":"<<copyBytes<<"}\n";
    std::uint64_t frames=0,rounds=0;
    auto sample=[&](std::string_view phase){out<<"{\"type\":\"sample\",\"phase\":"<<quote(phase)<<",\"elapsed\":"<<elapsed()<<",\"frames\":"<<frames<<",\"rounds\":"<<rounds;writeStats(out,take(d->resourceStatistics()));observer.write(out);out<<"}\n";out.flush();};
    sample("baseline");
    {void* pages=VirtualAlloc(nullptr,32*MiB,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);check(pages!=nullptr,"CPU positive control allocation");std::memset(pages,0x5a,32*MiB);sample("cpu_held");check(VirtualFree(pages,0,MEM_RELEASE)!=0,"CPU positive control release");}sample("cpu_released");
    {auto held=take(d->createBuffer({64*MiB}));{auto upload=take(d->createBuffer({64*MiB,gfx::Memory::Upload}));auto c=take(d->createCommandList());take(c->begin());take(c->transition(held,S::CopyDestination));take(c->copyBuffer(upload,held,64*MiB));take(c->end());take(d->queue().wait(take(d->queue().submit(*c))));}take(d->queue().waitIdle());sample("gpu_held");}take(d->queue().waitIdle());sample("gpu_released");
    bool wrongThread=false;std::thread thread([&]{auto r=d->resourceStatistics();wrongThread=!r&&r.error().code==gfx::ErrorCode::InvalidState;});thread.join();check(wrongThread,"Statistics must obey device thread affinity");
    const auto pd=pipelineDesc();auto workStart=Clock::now();double next=0;bool measured=false;Clock::time_point measureStart{};
    bool aborted=false;
    while(true){
        if(!measured && std::chrono::duration<double>(Clock::now()-workStart).count()>=warmup){measured=true;measureStart=Clock::now();next=0;sample("measurement_start");}
        auto p=take(d->createGraphicsPipeline(pd));std::vector<Frame> batch;
        for(unsigned i=0;i<ring;++i)batch.push_back(submit(*d,p,static_cast<unsigned>(frames+i)));
        const double t=measured?std::chrono::duration<double>(Clock::now()-measureStart).count():-1;
        const bool report=measured&&t>=next;
        if(report)sample("live");
        take(d->queue().wait(batch.back().fence));for(const auto& f:batch)verify(f);frames+=ring;++rounds;
        batch.clear();p.reset();take(d->queue().waitIdle());
        auto stats=take(d->resourceStatistics());check(stats.trackedObjects==0&&stats.uniqueResources==0&&stats.resourceAllocationBytes==0&&stats.pendingSubmissions==0&&stats.pendingObjectReferences==0,"Drained SDK ownership did not return to zero");
        if(report){sample("drained");next+=interval;std::cout<<mode<<" seconds="<<t<<" frames="<<frames<<" reclaimed=0\n"<<std::flush;}
        // Observe after every verified, drained round, including warmup. This is a
        // process-private growth guard, not a hard allocation ceiling or a peak meter.
        const auto privateNow=processMemory().PrivateUsage;
        if(privateNow>baselinePrivate&&privateNow-baselinePrivate>guardBytes){aborted=true;sample("guard_aborted");break;}
        if(measured&&std::chrono::duration<double>(Clock::now()-measureStart).count()>=seconds)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    sample("final");const auto errors=mode=="validation"?take(d->validationErrorCount()):0;check(errors==0,"Unexpected D3D12 debug/GBV errors");
    out<<"{\"type\":\"footer\",\"adapter_id\":"<<d->caps().adapterId.value<<",\"elapsed\":"<<elapsed()<<",\"measured_seconds\":"<<(measured?std::chrono::duration<double>(Clock::now()-measureStart).count():0)<<",\"frames\":"<<frames<<",\"full_images\":"<<frames*3<<",\"verified_buffer_bytes\":"<<frames*copyBytes<<",\"validation_errors\":"<<errors<<",\"statistics_contracts\":true,\"status\":"<<quote(aborted?"aborted":"passed")<<",\"stop_reason\":"<<quote(aborted?"private_growth_limit":"completed")<<"}\n";out.flush();std::cout<<(aborted?"ABORT: private growth guard; ":"PASS: ")<<frames<<" frames, "<<frames*3<<" full images, reclaimed ownership zero\n";return aborted?2:0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
