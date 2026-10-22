// Diagnostic only: share the exact render/readback helpers with the failed soak.
// Native device access below observes the debug layer; workloads still use the SDK.
#define main memoryStressEntryUnused
#include "../MemoryStress.cpp"
#undef main
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include "ReusableFrame.hpp"

namespace {
struct NativeMessages {
    ComPtr<ID3D12InfoQueue1> source;DWORD cookie{};std::atomic<std::uint64_t> errors{};
    static void CALLBACK message(D3D12_MESSAGE_CATEGORY,D3D12_MESSAGE_SEVERITY severity,D3D12_MESSAGE_ID,LPCSTR,void* context){if(severity==D3D12_MESSAGE_SEVERITY_ERROR||severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)static_cast<NativeMessages*>(context)->errors.fetch_add(1,std::memory_order_relaxed);}
    explicit NativeMessages(ID3D12Device& device){hr(device.QueryInterface(IID_PPV_ARGS(&source)));hr(source->RegisterMessageCallback(message,D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,this,&cookie));}
    ~NativeMessages(){if(source)(void)source->UnregisterMessageCallback(cookie);}
};
void nativeTextureControl(std::ostream& out,std::string_view mode,unsigned seconds){
    ComPtr<ID3D12Debug> debug;hr(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();ComPtr<ID3D12Debug1> debug1;hr(debug.As(&debug1));const bool gpu=mode!="native-cpu-texture";debug1->SetEnableGPUBasedValidation(gpu);
    if(mode=="native-gbv-no-tracking"){ComPtr<ID3D12Debug2> debug2;hr(debug.As(&debug2));debug2->SetGPUBasedValidationFlags(D3D12_GPU_BASED_VALIDATION_FLAGS_DISABLE_STATE_TRACKING);}
    ComPtr<IDXGIFactory4> factory;hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;DXGI_ADAPTER_DESC1 desc{};
    for(UINT i=0;;++i){hr(factory->EnumAdapters1(i,&adapter));hr(adapter->GetDesc1(&desc));if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))break;adapter.Reset();}
    gfx::DeviceCaps caps;caps.adapterName=utf8(desc.Description);Observer observer(caps);ComPtr<ID3D12Device> device;hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));NativeMessages messages(*device.Get());
    const auto start=Clock::now();unsigned iterations=0;double next=0;
    out<<"{\"isolation_case\":"<<quote(mode)<<",\"native_only\":true,\"gpu_validation\":"<<(gpu?"true":"false")<<",\"gpu_state_tracking\":"<<(mode=="native-gbv-no-tracking"?"false":"true")<<",\"adapter\":"<<quote(caps.adapterName)<<",\"luid_high\":"<<desc.AdapterLuid.HighPart<<",\"luid_low\":"<<desc.AdapterLuid.LowPart<<"}\n";
    auto sample=[&](std::string_view phase){out<<"{\"phase\":"<<quote(phase)<<",\"elapsed\":"<<std::chrono::duration<double>(Clock::now()-start).count()<<",\"iterations\":"<<iterations;observer.write(out);out<<"}\n";out.flush();};sample("baseline");
    while(iterations<1024&&std::chrono::duration<double>(Clock::now()-start).count()<seconds){for(unsigned i=0;i<3;++i){
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=d.Height=side;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Format=i==1?DXGI_FORMAT_D32_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;d.Flags=i==0?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:i==1?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL|D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE:D3D12_RESOURCE_FLAG_NONE;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> resource;hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource)));
    }++iterations;auto t=std::chrono::duration<double>(Clock::now()-start).count();if(t>=next){sample("sample");next+=1;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    sample("released");check(messages.errors.load()==0,"Native-only allocation validation error");out<<"{\"isolation_passed\":true,\"native_only\":true,\"validation_errors\":0,\"iterations\":"<<iterations<<",\"resource_creations\":"<<iterations*3<<"}\n";out.flush();
}
Frame pooledSubmit(gfx::Device& d,const gfx::PipelinePtr& pipeline,unsigned index,gfx::CommandList& commands){
    auto a=take(d.createTexture({side,side,gfx::Format::RGBA8UNorm,true})),b=take(d.createTexture({side,side,gfx::Format::RGBA8UNorm,true})),z=take(d.createTexture({side,side,gfx::Format::D32Float,false,true}));
    auto image=take(d.createTexture({side,side}));auto f=a->footprint();
    auto upload=take(d.createBuffer({f.size,gfx::Memory::Upload}));std::vector<std::byte> white(f.size,std::byte{255});take(upload->write(0,white));
    auto cb=take(d.createBuffer({512,gfx::Memory::Upload}));const std::array<float,4> color{index%2?0.f:1.f,index%2?1.f:0.f,0,1};take(cb->write(0,std::as_bytes(std::span{color})));take(cb->write(256,std::as_bytes(std::span{color})));
    auto input=take(d.createBuffer({copyBytes,gfx::Memory::Upload})),device=take(d.createBuffer({copyBytes}));std::vector<std::byte> pattern(copyBytes,std::byte(index%251));take(input->write(0,pattern));
    Frame r{take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({copyBytes,gfx::Memory::Readback})),f,{},index};
    auto* c=&commands;take(c->begin());take(c->transition(image,S::CopyDestination));take(c->copyBufferToTexture(upload,image));take(c->transition(image,S::ShaderRead));take(c->transition(cb,S::ConstantBuffer));
    take(c->transition(a,S::RenderTarget));take(c->transition(b,S::RenderTarget));take(c->transition(z,S::DepthWrite));take(c->beginRenderPass({{{a},{b}},{z}}));take(c->setPipeline(pipeline));
    take(c->bindResources({{{0,0,image}},{{1,0,cb,0,256}}}));take(c->setScissor(0,0,side/2,side));take(c->draw(3));
    take(c->bindResources({{{0,0,image}},{{1,0,cb,256,256}}}));take(c->setScissor(side/2,0,side/2,side));take(c->draw(3));take(c->endRenderPass());
    take(c->transition(a,S::CopySource));take(c->copyTextureToBuffer(a,r.a));take(c->transition(b,S::CopySource));take(c->copyTextureToBuffer(b,r.b));take(c->transition(z,S::CopySource));take(c->copyTextureToBuffer(z,r.z));
    take(c->transition(device,S::CopyDestination));take(c->copyBuffer(input,device,copyBytes));take(c->transition(device,S::CopySource));take(c->copyBuffer(device,r.copy,copyBytes));
    take(c->writeTimestamp(0));take(c->writeTimestamp(1));take(c->resolveTimestamps());take(c->end());r.fence=take(d.queue().submit(*c));return r;
}
bool allocationCase(std::string_view mode){return mode=="sdk-allocate"||mode=="native-allocate"||mode=="idle-only"||mode=="submit-empty"||mode=="copy-new"||mode=="copy-reuse"||mode=="texture-allocate"||mode=="native-texture"||mode=="native-texture-views"||mode=="native-footprints"||mode=="footprints-only"||mode=="native-placed"||mode=="native-not-zeroed"||mode=="native-optimized-clear";}
void isolate(gfx::Device& d,ID3D12Device& native,Observer& observer,std::ostream& out,std::string_view mode,unsigned seconds){
    const auto start=Clock::now();unsigned iterations=0;std::uint64_t creations=0;double next=0;
    std::unique_ptr<gfx::CommandList> c;
    if(mode=="submit-empty"||mode=="copy-new"||mode=="copy-reuse")c=take(d.createCommandList());
    gfx::BufferPtr upload,device,readback;
    if(mode=="copy-reuse"){upload=take(d.createBuffer({copyBytes,gfx::Memory::Upload}));device=take(d.createBuffer({copyBytes}));readback=take(d.createBuffer({copyBytes,gfx::Memory::Readback}));creations+=3;}
    auto sample=[&](std::string_view phase){out<<"{\"phase\":"<<quote(phase)<<",\"elapsed\":"<<std::chrono::duration<double>(Clock::now()-start).count()<<",\"iterations\":"<<iterations<<",\"resource_creations\":"<<creations;writeStats(out,take(d.resourceStatistics()));observer.write(out);out<<"}\n";out.flush();};
    out<<"{\"isolation_case\":"<<quote(mode)<<",\"sdk_sha256\":"<<quote(gfx::buildFingerprint())<<",\"seconds\":"<<seconds<<"}\n";sample("baseline");
    while(iterations<1024&&std::chrono::duration<double>(Clock::now()-start).count()<seconds){
        if(mode=="sdk-allocate"||mode=="native-allocate")for(unsigned i=0;i<8;++i){
            const auto memory=static_cast<gfx::Memory>(i%3);
            if(mode=="sdk-allocate"){auto resource=take(d.createBuffer({65536,memory}));}
            else{D3D12_HEAP_PROPERTIES heap{};heap.Type=memory==gfx::Memory::Device?D3D12_HEAP_TYPE_DEFAULT:memory==gfx::Memory::Upload?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=65536;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> resource;
                hr(native.CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,memory==gfx::Memory::Device?D3D12_RESOURCE_STATE_COMMON:memory==gfx::Memory::Upload?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&resource)));}
            ++creations;
        }
        if(mode=="idle-only")take(d.queue().waitIdle());
        if(mode=="texture-allocate")for(auto desc:{gfx::TextureDesc{side,side,gfx::Format::RGBA8UNorm,true},gfx::TextureDesc{side,side,gfx::Format::D32Float,false,true},gfx::TextureDesc{side,side}}){auto texture=take(d.createTexture(desc));++creations;}
        if(mode=="native-texture"||mode=="native-texture-views"||mode=="native-footprints"||mode=="footprints-only"||mode=="native-placed"||mode=="native-not-zeroed"||mode=="native-optimized-clear")for(unsigned i=0;i<3;++i){
            D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=desc.Height=side;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=i==1?DXGI_FORMAT_D32_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;desc.Flags=i==0?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:i==1?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL:D3D12_RESOURCE_FLAG_NONE;
            ComPtr<ID3D12Heap> allocation;ComPtr<ID3D12Resource> resource;
            if(mode!="footprints-only"){D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
                if(mode=="native-placed"){auto size=native.GetResourceAllocationInfo(0,1,&desc);check(size.SizeInBytes!=UINT64_MAX,"Placed allocation info");D3D12_HEAP_DESC hd{};hd.SizeInBytes=size.SizeInBytes;hd.Alignment=size.Alignment;hd.Properties=heap;hd.Flags=i==2?D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES:D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;hr(native.CreateHeap(&hd,IID_PPV_ARGS(&allocation)));hr(native.CreatePlacedResource(allocation.Get(),0,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource)));}
                else{D3D12_CLEAR_VALUE clear{};clear.Format=desc.Format;if(i==1)clear.DepthStencil.Depth=1.f;hr(native.CreateCommittedResource(&heap,mode=="native-not-zeroed"?D3D12_HEAP_FLAG_CREATE_NOT_ZEROED:D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,mode=="native-optimized-clear"&&i<2?&clear:nullptr,IID_PPV_ARGS(&resource)));}++creations;}
            if(mode=="native-footprints"||mode=="footprints-only"){D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows{};UINT64 rowSize{},total{};native.GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowSize,&total);check(total!=UINT64_MAX&&rows==side,"Native texture footprint");}
            if(mode=="native-texture-views"&&i<2){ComPtr<ID3D12DescriptorHeap> view;D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=i==0?D3D12_DESCRIPTOR_HEAP_TYPE_RTV:D3D12_DESCRIPTOR_HEAP_TYPE_DSV;heap.NumDescriptors=i==0?1:2;hr(native.CreateDescriptorHeap(&heap,IID_PPV_ARGS(&view)));auto handle=view->GetCPUDescriptorHandleForHeapStart();
                if(i==0)native.CreateRenderTargetView(resource.Get(),nullptr,handle);else{D3D12_DEPTH_STENCIL_VIEW_DESC v{};v.Format=DXGI_FORMAT_D32_FLOAT;v.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;native.CreateDepthStencilView(resource.Get(),&v,handle);handle.ptr+=native.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);v.Flags=D3D12_DSV_FLAG_READ_ONLY_DEPTH;native.CreateDepthStencilView(resource.Get(),&v,handle);}}
        }
        if(mode=="submit-empty"){take(c->begin());take(c->end());take(d.queue().wait(take(d.queue().submit(*c))));take(d.queue().waitIdle());}
        if(mode=="copy-new"||mode=="copy-reuse"){
            if(mode=="copy-new"){upload=take(d.createBuffer({copyBytes,gfx::Memory::Upload}));device=take(d.createBuffer({copyBytes}));readback=take(d.createBuffer({copyBytes,gfx::Memory::Readback}));creations+=3;}
            std::vector<std::byte> input(copyBytes,std::byte(iterations%251));take(upload->write(0,input));take(c->begin());take(c->transition(device,S::CopyDestination));take(c->copyBuffer(upload,device,copyBytes));take(c->transition(device,S::CopySource));take(c->copyBuffer(device,readback,copyBytes));take(c->end());take(d.queue().wait(take(d.queue().submit(*c))));
            check(take(readback->read(0,copyBytes))==input,"Isolated full buffer reference");take(c->begin());take(c->end());if(mode=="copy-new"){upload.reset();device.reset();readback.reset();}take(d.queue().waitIdle());
        }
        ++iterations;const auto t=std::chrono::duration<double>(Clock::now()-start).count();if(t>=next){sample("sample");next+=1;}std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    c.reset();upload.reset();device.reset();readback.reset();take(d.queue().waitIdle());sample("released");check(take(d.validationErrorCount())==0,"Isolated native validation error");
    const auto s=take(d.resourceStatistics());check(s.trackedObjects==0&&s.uniqueResources==0&&s.resourceAllocationBytes==0&&s.pendingSubmissions==0&&s.pendingObjectReferences==0,"Isolated SDK ownership did not drain");
    out<<"{\"isolation_passed\":true,\"validation_errors\":0,\"iterations\":"<<iterations<<",\"resource_creations\":"<<creations<<"}\n";out.flush();
}
Frame renderOnly(gfx::Device& d,gfx::CommandList& c,const gfx::PipelinePtr& p){
    auto a=take(d.createTexture({side,side,gfx::Format::RGBA8UNorm,true})),b=take(d.createTexture({side,side,gfx::Format::RGBA8UNorm,true})),z=take(d.createTexture({side,side,gfx::Format::D32Float,false,true}));const auto f=a->footprint();
    Frame r{take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({copyBytes,gfx::Memory::Readback})),f,{},0};
    auto upload=take(d.createBuffer({copyBytes,gfx::Memory::Upload}));std::vector<std::byte> zero(copyBytes);take(upload->write(0,zero));
    take(c.begin());take(c.transition(a,S::RenderTarget));take(c.transition(b,S::RenderTarget));take(c.transition(z,S::DepthWrite));
    gfx::RenderPassDesc pass{{{a,gfx::LoadOp::Clear,p?gfx::Color{0,0,1,1}:gfx::Color{1,0,0,1}},{b,gfx::LoadOp::Clear,p?gfx::Color{0,0,1,1}:gfx::Color{0,1,1,1}}},{z,gfx::LoadOp::Clear,p?1.f:.5f}};
    take(c.beginRenderPass(pass));if(p){take(c.setPipeline(p));take(c.draw(3));}take(c.endRenderPass());
    take(c.transition(a,S::CopySource));take(c.copyTextureToBuffer(a,r.a));take(c.transition(b,S::CopySource));take(c.copyTextureToBuffer(b,r.b));take(c.transition(z,S::CopySource));take(c.copyTextureToBuffer(z,r.z));take(c.copyBuffer(upload,r.copy,copyBytes));take(c.end());r.fence=take(d.queue().submit(c));return r;
}
void isolateRendering(gfx::Device& d,Observer& observer,std::ostream& out,std::string_view mode,unsigned seconds){
    auto c=take(d.createCommandList());gfx::PipelinePtr p;if(mode=="draw-new"){
        auto pd=pipelineDesc();pd.fragment=take(gfx::compileHlsl("struct Out{float4 a:SV_Target0;float4 b:SV_Target1;};Out PS(){Out o;o.a=float4(1,0,0,1);o.b=float4(0,1,1,1);return o;}","PS",gfx::ShaderStage::Fragment));pd.bindingLayout={};p=take(d.createGraphicsPipeline(pd));}
    auto start=Clock::now();unsigned frames=0;double next=0;
    auto sample=[&](std::string_view phase){out<<"{\"phase\":"<<quote(phase)<<",\"elapsed\":"<<std::chrono::duration<double>(Clock::now()-start).count()<<",\"frames\":"<<frames;writeStats(out,take(d.resourceStatistics()));observer.write(out);out<<"}\n";out.flush();};
    out<<"{\"isolation_case\":"<<quote(mode)<<",\"sdk_sha256\":"<<quote(gfx::buildFingerprint())<<"}\n";sample("baseline");
    while(std::chrono::duration<double>(Clock::now()-start).count()<seconds){auto r=renderOnly(d,*c,p);take(d.queue().wait(r.fence));verify(r);take(c->begin());take(c->end());++frames;auto t=std::chrono::duration<double>(Clock::now()-start).count();if(t>=next){sample("sample");next+=1;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    c.reset();p.reset();take(d.queue().waitIdle());sample("released");check(take(d.validationErrorCount())==0,"Render isolation native validation error");out<<"{\"isolation_passed\":true,\"validation_errors\":0,\"frames\":"<<frames<<",\"full_images\":"<<frames*3<<"}\n";out.flush();
}
void bounded(gfx::Device& d,Observer& observer,std::ostream& out,unsigned seconds){
    const auto start=Clock::now();std::uint64_t frames=0;const unsigned warmup=seconds>=300?30:1;double next=0;bool measuring=false;Clock::time_point measured;
    auto sample=[&](std::string_view phase){out<<"{\"phase\":"<<quote(phase)<<",\"elapsed\":"<<std::chrono::duration<double>(Clock::now()-start).count()<<",\"frames\":"<<frames;writeStats(out,take(d.resourceStatistics()));observer.write(out);out<<"}\n";out.flush();};
    out<<"{\"case\":\"bounded-reuse\",\"schema\":1,\"sdk_sha256\":"<<quote(gfx::buildFingerprint())<<",\"seconds\":"<<seconds<<",\"warmup\":"<<warmup<<",\"ring\":"<<ring<<",\"side\":"<<side<<",\"copy_bytes\":"<<copyBytes<<",\"gpu_state_tracking\":true,\"adapter\":"<<quote(d.caps().adapterName)<<",\"luid_high\":"<<observer.desc.AdapterLuid.HighPart<<",\"luid_low\":"<<observer.desc.AdapterLuid.LowPart<<"}\n";sample("baseline");
    {void* pages=VirtualAlloc(nullptr,32*MiB,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);check(pages!=nullptr,"Bounded CPU control");std::memset(pages,0x5a,32*MiB);sample("cpu_held");check(VirtualFree(pages,0,MEM_RELEASE)!=0,"Bounded CPU control release");}sample("cpu_released");
    {auto resource=take(d.createBuffer({64*MiB}));{auto upload=take(d.createBuffer({64*MiB,gfx::Memory::Upload}));auto c=take(d.createCommandList());take(c->begin());take(c->transition(resource,S::CopyDestination));take(c->copyBuffer(upload,resource,64*MiB));take(c->end());take(d.queue().wait(take(d.queue().submit(*c))));}take(d.queue().waitIdle());sample("gpu_held");}take(d.queue().waitIdle());sample("gpu_released");
    auto p=take(d.createGraphicsPipeline(pipelineDesc()));std::vector<ReusableFrame> slots;for(unsigned i=0;i<ring;++i)slots.emplace_back(d);const auto owned=take(d.resourceStatistics());check(owned.trackedObjects==105&&owned.uniqueResources==104&&owned.pendingSubmissions==0&&owned.pendingObjectReferences==0,"Bounded pool accounting");sample("pool_ready");const auto workStart=Clock::now();
    while(true){
        if(!measuring&&std::chrono::duration<double>(Clock::now()-workStart).count()>=warmup){measuring=true;measured=Clock::now();sample("measurement_start");}
        for(unsigned i=0;i<ring;++i)slots[i].submit(d,p,static_cast<unsigned>(frames+i));take(d.queue().wait(slots.back().result.fence));for(const auto& s:slots)verify(s.result);frames+=ring;take(d.queue().waitIdle());const auto now=take(d.resourceStatistics());
        check(now.trackedObjects==owned.trackedObjects&&now.uniqueResources==owned.uniqueResources&&now.resourceAllocationBytes==owned.resourceAllocationBytes&&now.pendingSubmissions==0&&now.pendingObjectReferences==0,"Fixed resource set changed or failed to drain");
        const auto t=measuring?std::chrono::duration<double>(Clock::now()-measured).count():-1;if(measuring&&t>=next){sample("drained");next+=5;std::cout<<"bounded-reuse seconds="<<t<<" frames="<<frames<<'\n'<<std::flush;}
        if(measuring&&t>=seconds)break;std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    slots.clear();p.reset();take(d.queue().waitIdle());sample("released");const auto empty=take(d.resourceStatistics());check(empty.trackedObjects==0&&empty.uniqueResources==0&&empty.resourceAllocationBytes==0&&empty.pendingSubmissions==0&&empty.pendingObjectReferences==0,"Bounded pool final release");check(take(d.validationErrorCount())==0,"Bounded workload validation error");
    out<<"{\"functional_passed\":true,\"validation_errors\":0,\"validation_counter\":\"unfiltered_callback\",\"measured_seconds\":"<<std::chrono::duration<double>(Clock::now()-measured).count()<<",\"frames\":"<<frames<<",\"full_images\":"<<frames*3<<",\"verified_buffer_bytes\":"<<frames*copyBytes<<"}\n";out.flush();
}
}
int main(int argc,char** argv)try{
    check(argc==4,"Usage: memory_validation_probe case seconds output.jsonl");const std::string mode=argv[1],output=argv[3];const unsigned seconds=std::stoul(argv[2]);
    const bool nativeOnly=mode=="native-cpu-texture"||mode=="native-gbv-texture"||mode=="native-gbv-no-tracking";
    check((mode=="reuse-pipeline"||mode=="reuse-commands"||mode=="churn-pipeline"||mode=="counter-contract"||mode=="clear-new"||mode=="draw-new"||mode=="bounded-reuse"||allocationCase(mode)||nativeOnly)&&seconds>0&&seconds<=(mode=="bounded-reuse"?3600u:120u),"Invalid diagnostic case");std::ifstream exists(output);check(!exists.good(),"Refuse overwrite");exists.close();std::ofstream out(output);out.exceptions(std::ios::badbit|std::ios::failbit);out<<std::setprecision(12);
    if(nativeOnly){nativeTextureControl(out,mode,seconds);return 0;}
    auto d=take(gfx::createDevice({gfx::Backend::D3D12,true}));Observer observer(d->caps());ComPtr<ID3D12Device> native;hr(D3D12CreateDevice(observer.adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&native)));ComPtr<ID3D12InfoQueue> info;hr(native.As(&info));
    if(mode=="bounded-reuse"){bounded(*d,observer,out,seconds);return 0;}
    if(allocationCase(mode)){isolate(*d,*native.Get(),observer,out,mode,seconds);std::cout<<"PASS isolation "<<mode<<'\n';return 0;}
    if(mode=="clear-new"||mode=="draw-new"){isolateRendering(*d,observer,out,mode,seconds);return 0;}
    if(mode=="counter-contract"){
        hr(info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,FALSE));hr(info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION,FALSE));hr(info->SetMessageCountLimit(8));
        for(unsigned i=0;i<64;++i)hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_WARNING,"Intentional queue saturation"));
        check(info->GetNumMessagesDiscardedByMessageCountLimit()>0&&take(d->validationErrorCount())==0,"Warning overflow does not invent errors");
        hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"Intentional overflowed error"));check(take(d->validationErrorCount())==1,"Overflowed error must be counted");
        info->ClearStoredMessages();check(take(d->validationErrorCount())==1,"Clearing native text cannot reset cumulative errors");
        D3D12_MESSAGE_SEVERITY denied[]{D3D12_MESSAGE_SEVERITY_ERROR,D3D12_MESSAGE_SEVERITY_CORRUPTION};D3D12_INFO_QUEUE_FILTER filter{};filter.DenyList.NumSeverities=2;filter.DenyList.pSeverityList=denied;hr(info->AddStorageFilterEntries(&filter));hr(info->AddRetrievalFilterEntries(&filter));
        hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"Intentional filtered error"));hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_CORRUPTION,"Intentional filtered corruption"));check(take(d->validationErrorCount())==3,"Filters cannot hide ERROR/CORRUPTION");
        HRESULT workerResult=E_FAIL;std::thread worker([&]{workerResult=info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"Intentional worker-thread error");});worker.join();hr(workerResult);check(take(d->validationErrorCount())==4,"Worker callback is thread safe");
        {auto second=take(gfx::createDevice({gfx::Backend::D3D12,true}));check(take(second->validationErrorCount())==0,"Independent registration starts at zero");hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"Intentional shared native device error"));check(take(d->validationErrorCount())==5&&take(second->validationErrorCount())==1,"Shared native owners keep independent callback lifetimes");}
        hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"Intentional error after other owner destruction"));check(take(d->validationErrorCount())==6,"Other owner teardown preserves callback");
        d.reset();hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"No callback context after SDK destruction"));
        out<<"{\"counter_contract_passed\":true,\"expected_injected_errors\":6,\"overflow_filter_clear_worker_shared_owner_teardown\":true,\"sdk_sha256\":"<<quote(gfx::buildFingerprint())<<"}\n";out.flush();std::cout<<"PASS: 6 intentional errors counted across overflow/filter/clear/worker/shared-owner teardown\n";return 0;
    }
    const auto pd=pipelineDesc();auto p=take(d->createGraphicsPipeline(pd));std::vector<std::unique_ptr<gfx::CommandList>> pool;if(mode!="reuse-pipeline")for(unsigned i=0;i<ring;++i)pool.push_back(take(d->createCommandList()));
    auto start=Clock::now();std::uint64_t frames=0;double next=0;
    auto sample=[&](std::string_view phase){out<<"{\"phase\":"<<quote(phase)<<",\"elapsed\":"<<std::chrono::duration<double>(Clock::now()-start).count()<<",\"frames\":"<<frames<<",\"stored_messages\":"<<info->GetNumStoredMessages()<<",\"discarded_messages\":"<<info->GetNumMessagesDiscardedByMessageCountLimit();writeStats(out,take(d->resourceStatistics()));observer.write(out);out<<"}\n";out.flush();};
    out<<"{\"diagnostic_only\":true,\"case\":"<<quote(mode)<<",\"sdk_sha256\":"<<quote(gfx::buildFingerprint())<<",\"seconds\":"<<seconds<<"}\n";sample("baseline");
    while(std::chrono::duration<double>(Clock::now()-start).count()<seconds){
        if(mode=="churn-pipeline")p=take(d->createGraphicsPipeline(pd));std::vector<Frame> batch;
        for(unsigned i=0;i<ring;++i)batch.push_back(pool.empty()?submit(*d,p,static_cast<unsigned>(frames+i)):pooledSubmit(*d,p,static_cast<unsigned>(frames+i),*pool[i]));
        take(d->queue().wait(batch.back().fence));for(const auto& r:batch)verify(r);frames+=ring;batch.clear();
        for(auto& c:pool){take(c->begin());take(c->end());}take(d->queue().waitIdle());
        const auto t=std::chrono::duration<double>(Clock::now()-start).count();if(t>=next){sample("drained");next+=1;}
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    pool.clear();p.reset();take(d->queue().waitIdle());sample("released");auto errors=take(d->validationErrorCount());check(errors==0,"Diagnostic workload native validation errors");
    // Save the native external-reference view before clearing messages. Do not hide errors.
    ComPtr<ID3D12DebugDevice> debug;hr(native.As(&debug));hr(debug->ReportLiveDeviceObjects(static_cast<D3D12_RLDO_FLAGS>(D3D12_RLDO_DETAIL|D3D12_RLDO_IGNORE_INTERNAL)));
    std::ofstream messages(output+".native.txt");messages.exceptions(std::ios::badbit|std::ios::failbit);
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size{};hr(info->GetMessage(i,nullptr,&size));std::vector<std::byte> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());hr(info->GetMessage(i,message,&size));messages<<message->ID<<" severity="<<message->Severity<<" "<<message->pDescription<<'\n';}
    info->ClearStoredMessages();sample("after_message_clear");
    out<<"{\"functional_passed\":true,\"memory_acceptance_claim\":false,\"validation_errors\":"<<errors<<",\"frames\":"<<frames<<",\"full_images\":"<<frames*3<<"}\n";out.flush();std::cout<<"PASS diagnostic references: "<<mode<<" frames="<<frames<<" (not soak acceptance)\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
