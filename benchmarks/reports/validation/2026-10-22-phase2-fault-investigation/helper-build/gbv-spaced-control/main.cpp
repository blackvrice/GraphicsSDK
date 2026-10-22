// Temporary SDK-free causal intervention, using the existing native lifetime helper.
#define main unusedLifetimeEntry
#include "../../tools/memory_validation_probe/Lifetime.cpp"
#undef main
#include <d3d12.h>
#include <map>
#include <vector>
namespace {
void memoryMap(std::ostream& out){
    std::uint64_t priv=0,mapped=0,image=0;unsigned regions=0;MEMORY_BASIC_INFORMATION m{};
    for(std::uintptr_t p=0;VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof(m))==sizeof(m);){
        if(m.State==MEM_COMMIT){++regions;if(m.Type==MEM_PRIVATE)priv+=m.RegionSize;else if(m.Type==MEM_MAPPED)mapped+=m.RegionSize;else if(m.Type==MEM_IMAGE)image+=m.RegionSize;}
        const auto next=reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize;if(next<=p)break;p=next;
    }
    out<<",\"committed_private_regions_bytes\":"<<priv<<",\"committed_mapped_regions_bytes\":"<<mapped<<",\"committed_image_regions_bytes\":"<<image<<",\"committed_regions\":"<<regions;
}
void origin(std::ostream& out,std::string_view mode,unsigned index,unsigned batches,unsigned count,const std::string& handshake){
    ComPtr<ID3D12Debug> debug;hr(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<ID3D12Debug1> d1;hr(debug.As(&d1));d1->SetEnableGPUBasedValidation(mode!="cpu");
    ComPtr<ID3D12Debug2> d2;hr(debug.As(&d2));d2->SetGPUBasedValidationFlags(mode=="no-tracking"?D3D12_GPU_BASED_VALIDATION_FLAGS_DISABLE_STATE_TRACKING:D3D12_GPU_BASED_VALIDATION_FLAGS_NONE);
    ComPtr<IDXGIFactory4> factory;hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    for(UINT i=0,n=0;;++i){hr(factory->EnumAdapters1(i,&adapter));DXGI_ADAPTER_DESC1 desc{};hr(adapter->GetDesc1(&desc));if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&n++==index)break;adapter.Reset();}
    DXGI_ADAPTER_DESC1 ad{};hr(adapter->GetDesc1(&ad));ComPtr<IDXGIAdapter3> observer;hr(adapter.As(&observer));
    ComPtr<ID3D12Device> device;hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> fence;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;HANDLE done=nullptr;UINT64 timeline=0;
    if(mode=="full-drain"){
        D3D12_COMMAND_QUEUE_DESC desc{};hr(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)));hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));hr(list->Close());
        done=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(done!=nullptr,"Queue event");
    }
    static Counts counts;{ComPtr<ID3DDestructionNotifier> notify;hr(device.As(&notify));UINT cookie{};hr(notify->RegisterDestructionCallback(Counts::device,&counts,&cookie));}
    const auto start=Clock::now();unsigned created=0,completed=0;
    out<<"{\"type\":\"header\",\"pid\":"<<GetCurrentProcessId()<<",\"native_only\":true,\"adapter\":"<<quote(utf8(ad.Description))<<",\"mode\":"<<quote(mode)<<",\"luid_high\":"<<ad.AdapterLuid.HighPart<<",\"luid_low\":"<<ad.AdapterLuid.LowPart<<",\"batches\":"<<batches<<",\"iterations\":"<<count<<"}\n";out.flush();
    auto sample=[&](std::string_view phase){DXGI_QUERY_VIDEO_MEMORY_INFO local{},nonlocal{};const auto a=observer->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local),b=observer->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&nonlocal);hr(a);hr(b);
        out<<"{\"type\":\"sample\",\"phase\":"<<quote(phase)<<",\"elapsed\":"<<std::chrono::duration<double>(Clock::now()-start).count()<<",\"batches\":"<<completed<<",\"private_bytes\":"<<privateBytes()<<",\"resources_created\":"<<created<<",\"resources_destroyed\":"<<counts.resources.load()<<",\"devices_destroyed\":"<<counts.devices.load()<<",\"validation_errors\":"<<counts.errors.load()<<",\"gpu_local_bytes\":"<<local.CurrentUsage<<",\"gpu_nonlocal_bytes\":"<<nonlocal.CurrentUsage;memoryMap(out);out<<"}\n";out.flush();};
    auto trim=[&]{HEAP_OPTIMIZE_RESOURCES_INFORMATION info{HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION,0};const bool ok=HeapSetInformation(nullptr,HeapOptimizeResources,&info,sizeof(info));out<<"{\"type\":\"heap_trim\",\"success\":"<<(ok?"true":"false")<<",\"win32_error\":"<<(ok?0:GetLastError())<<"}\n";out.flush();};
    {Messages messages(*device.Get(),counts);sample("baseline");
        if(!handshake.empty()){
            {std::ofstream ready(handshake+".ready");ready<<GetCurrentProcessId();}
            const auto deadline=Clock::now()+std::chrono::seconds(30);
            while(GetFileAttributesA((handshake+".go").c_str())==INVALID_FILE_ATTRIBUTES){check(Clock::now()<deadline,"Heap trace handshake timeout");Sleep(20);}
        }
        for(unsigned batch=0;batch<batches;++batch){
            for(unsigned i=0;i<count;++i)for(unsigned j=0;j<3;++j){auto temporary=texture(*device.Get(),j,counts);++created;}
            ++completed;check(created==counts.resources.load(),"Resource destruction mismatch");sample("resources_released");
            if(queue){hr(allocator->Reset());hr(list->Reset(allocator.Get(),nullptr));hr(list->Close());ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);hr(queue->Signal(fence.Get(),++timeline));hr(fence->SetEventOnCompletion(timeline,done));check(WaitForSingleObject(done,30000)==WAIT_OBJECT_0,"Queue drain timeout");sample("after_queue_drain");}
            sample("without_heap_trim");check(privateBytes()<1024*MiB,"Causal diagnostic budget exceeded");
            for(unsigned second=1;second<=20;++second){Sleep(1000);sample("between_batch_quiet");}
        }
        ComPtr<ID3D12DebugDevice> report;hr(device.As(&report));messages.queue->ClearStoredMessages();hr(report->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL));
        const auto stored=messages.queue->GetNumStoredMessages();for(UINT64 i=0;i<stored;++i){SIZE_T size{};hr(messages.queue->GetMessage(i,nullptr,&size));std::vector<std::byte> data(size);auto* msg=reinterpret_cast<D3D12_MESSAGE*>(data.data());hr(messages.queue->GetMessage(i,msg,&size));
            std::string text(msg->pDescription,msg->DescriptionByteLength?msg->DescriptionByteLength-1:0);for(auto& c:text)if(static_cast<unsigned char>(c)<32)c=' ';out<<"{\"type\":\"live_object\",\"message\":"<<quote(text)<<"}\n";}
        check(counts.errors.load()==0,"Unexpected native validation error");
        HMODULE loaded[1024]{};DWORD bytes{};check(EnumProcessModules(GetCurrentProcess(),loaded,sizeof(loaded),&bytes)&&bytes<=sizeof(loaded),"Own modules");
        for(unsigned i=0;i<bytes/sizeof(HMODULE);++i){MODULEINFO info{};wchar_t path[32768]{};check(GetModuleInformation(GetCurrentProcess(),loaded[i],&info,sizeof(info))&&GetModuleFileNameW(loaded[i],path,32768),"Own module map");out<<"{\"type\":\"module\",\"base\":"<<reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll)<<",\"bytes\":"<<info.SizeOfImage<<",\"path\":"<<quote(utf8(path))<<"}\n";}out.flush();
    }
    list.Reset();allocator.Reset();queue.Reset();fence.Reset();if(done)CloseHandle(done);device.Reset();sample("device_released");check(counts.devices.load()==1,"Native device destruction missing");sample("without_device_heap_trim");
    std::this_thread::sleep_for(std::chrono::seconds(1));sample("settled");
    if(mode.starts_with("full")){std::this_thread::sleep_for(std::chrono::seconds(14));sample("settled_15_seconds");}
    out<<"{\"type\":\"footer\",\"passed\":true,\"memory_acceptance\":false}\n";
}
}
int main(int argc,char** argv)try{check(argc==6||argc==7,"mode cpu|full|no-tracking index batches iterations output [trace-handshake-prefix]");const std::string mode=argv[1];check(mode=="cpu"||mode=="full"||mode=="full-drain"||mode=="no-tracking","Unknown mode");
    const auto index=std::stoul(argv[2]);check(index<2,"Invalid adapter index");const auto batches=positive(argv[3],2),iterations=positive(argv[4],256);
    check(GetFileAttributesA(argv[5])==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND,"Refuse overwrite");std::ofstream out(argv[5]);check(out.good(),"Open output");out.exceptions(std::ios::badbit|std::ios::failbit);origin(out,mode,index,batches,iterations,argc==7?argv[6]:"");return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
