// SDK-free runtime comparison. The factory configuration is local to this process.
#define main lifetimeProbeUnused
#include "Lifetime.cpp"
#undef main
#include <cstring>
#include <filesystem>
#include <vector>

namespace {
std::string modulePath(HMODULE module) {
    wchar_t path[32768]{};const auto n=GetModuleFileNameW(module,path,32768);
    check(n>0&&n<32768,"Module path unavailable");return utf8(path);
}
std::string provider(ID3D12DeviceConfiguration& configuration) {
    // Windows COM ABI: IUnknown slots 0..2, followed by GetDesc at slot 3.
    void** table{};std::memcpy(&table,&configuration,sizeof(table));HMODULE module{};
    check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(table[3]),&module)!=0,"Configuration provider module");
    return modulePath(module);
}
void loadedRuntimes(std::ostream& out) {
    HMODULE loaded[1024]{};DWORD bytes{};check(EnumProcessModules(GetCurrentProcess(),loaded,sizeof(loaded),&bytes)!=0&&bytes<=sizeof(loaded),"Enumerate loaded modules");
    bool first=true;out<<'[';
    for(DWORD i=0;i<bytes/sizeof(HMODULE);++i){const auto path=modulePath(loaded[i]);const auto name=std::filesystem::path(path).filename().wstring();
        if(_wcsicmp(name.c_str(),L"d3d12.dll")&&_wcsicmp(name.c_str(),L"D3D12Core.dll")&&_wcsicmp(name.c_str(),L"d3d12SDKLayers.dll")&&_wcsicmp(name.c_str(),L"d3d10warp.dll"))continue;
        if(!first)out<<',';first=false;out<<quote(path);
    }out<<']';
}
void compare(std::ostream& out,std::string_view source,std::string_view mode,unsigned version,const std::string& path,unsigned iterations) {
    static Counts counts;
    const bool gpu=mode!="hardware-cpu",warp=mode=="warp-gbv";
    ComPtr<ID3D12SDKConfiguration1> sdk;ComPtr<ID3D12DeviceFactory> factory;
    if(source=="agility") {
        hr(D3D12GetInterface(CLSID_D3D12SDKConfiguration,IID_PPV_ARGS(&sdk)));
        hr(sdk->CreateDeviceFactory(version,path.c_str(),IID_PPV_ARGS(&factory)));
    } else if(source=="inbox-factory")hr(D3D12GetInterface(CLSID_D3D12DeviceFactory,IID_PPV_ARGS(&factory)));
    ComPtr<ID3D12Debug> debug;
    if(factory)hr(factory->GetConfigurationInterface(CLSID_D3D12Debug,IID_PPV_ARGS(&debug)));
    else hr(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    debug->EnableDebugLayer();ComPtr<ID3D12Debug1> debug1;hr(debug.As(&debug1));debug1->SetEnableGPUBasedValidation(gpu);
    ComPtr<ID3D12Debug2> debug2;hr(debug.As(&debug2));debug2->SetGPUBasedValidationFlags(D3D12_GPU_BASED_VALIDATION_FLAGS_NONE);
    if(factory)hr(factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_NONE));
    ComPtr<IDXGIFactory4> dxgi;hr(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi)));ComPtr<IDXGIAdapter1> adapter;
    if(warp)hr(dxgi->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    else for(UINT i=0;;++i){hr(dxgi->EnumAdapters1(i,&adapter));DXGI_ADAPTER_DESC1 d{};hr(adapter->GetDesc1(&d));if(!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))break;adapter.Reset();}
    DXGI_ADAPTER_DESC1 ad{};hr(adapter->GetDesc1(&ad));ComPtr<IDXGIAdapter3> observer;hr(adapter.As(&observer));
    ComPtr<ID3D12Device> device;
    if(factory)hr(factory->CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    else hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    const auto actualAdapter=device->GetAdapterLuid();
    check(actualAdapter.HighPart==ad.AdapterLuid.HighPart&&actualAdapter.LowPart==ad.AdapterLuid.LowPart&&((ad.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)!=0)==warp,"Actual adapter differs from selection");
    ComPtr<ID3D12DeviceConfiguration> configuration;hr(device.As(&configuration));const auto actual=configuration->GetDesc();
    check((actual.Flags&D3D12_DEVICE_FLAG_DEBUG_LAYER_ENABLED)!=0,"Actual debug layer is disabled");
    check(((actual.Flags&D3D12_DEVICE_FLAG_GPU_BASED_VALIDATION_ENABLED)!=0)==gpu&&actual.GpuBasedValidationFlags==D3D12_GPU_BASED_VALIDATION_FLAGS_NONE,"Actual GBV configuration differs");
    check((actual.Flags&D3D12_DEVICE_FLAG_SYNCHRONIZED_COMMAND_QUEUE_VALIDATION_DISABLED)==0&&actual.NumEnabledExperimentalFeatures==0,"Unexpected reduced/experimental configuration");
    check(source!="agility"||actual.SDKVersion==version,"Requested runtime was not selected");
    out<<"{\"type\":\"header\",\"schema\":1,\"source\":"<<quote(source)<<",\"mode\":"<<quote(mode)<<",\"requested_sdk_version\":"<<version<<",\"actual_sdk_version\":"<<actual.SDKVersion<<",\"device_flags\":"<<actual.Flags<<",\"gpu_validation\":"<<(gpu?"true":"false")<<",\"gpu_validation_flags\":"<<actual.GpuBasedValidationFlags<<",\"experimental_features\":"<<actual.NumEnabledExperimentalFeatures<<",\"native_only\":true,\"iterations\":"<<iterations<<",\"adapter\":"<<quote(utf8(ad.Description))<<",\"software\":"<<(warp?"true":"false")<<",\"luid_high\":"<<ad.AdapterLuid.HighPart<<",\"luid_low\":"<<ad.AdapterLuid.LowPart<<",\"configuration_provider\":"<<quote(provider(*configuration.Get()))<<",\"runtime_path\":"<<quote(path)<<",\"runtime_modules\":";
    loadedRuntimes(out);out<<"}\n";out.flush();
    {ComPtr<ID3DDestructionNotifier> notifier;hr(device.As(&notifier));UINT cookie{};hr(notifier->RegisterDestructionCallback(Counts::device,&counts,&cookie));}
    const auto start=Clock::now();unsigned created=0,done=0;
    auto sample=[&](std::string_view phase) {
        DWORD handles{};check(GetProcessHandleCount(GetCurrentProcess(),&handles)!=0,"Process handle query");DXGI_QUERY_VIDEO_MEMORY_INFO local{},nonlocal{};
        const auto a=observer->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local),b=observer->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&nonlocal);
        out<<"{\"type\":\"sample\",\"phase\":"<<quote(phase)<<",\"elapsed\":"<<std::chrono::duration<double>(Clock::now()-start).count()<<",\"iterations\":"<<done<<",\"resource_creations\":"<<created<<",\"resource_destructions\":"<<counts.resources.load()<<",\"device_destructions\":"<<counts.devices.load()<<",\"validation_errors_total\":"<<counts.errors.load()<<",\"private_bytes\":"<<privateBytes()<<",\"handles\":"<<handles<<",\"gpu_local_hresult\":"<<a<<",\"gpu_nonlocal_hresult\":"<<b<<",\"gpu_local_bytes\":"<<(SUCCEEDED(a)?std::to_string(local.CurrentUsage):"null")<<",\"gpu_nonlocal_bytes\":"<<(SUCCEEDED(b)?std::to_string(nonlocal.CurrentUsage):"null")<<"}\n";out.flush();
    };
    {
        Messages messages(*device.Get(),counts);sample("baseline");
        hr(messages.queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,FALSE));hr(messages.queue->SetMessageCountLimit(8));
        for(unsigned i=0;i<64;++i)hr(messages.queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_WARNING,"Intentional runtime counter saturation"));
        check(messages.queue->GetNumMessagesDiscardedByMessageCountLimit()>0,"Counter storage saturation control");
        hr(messages.queue->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"Intentional overflowed runtime counter control"));
        check(counts.errors.load()==1,"Unfiltered runtime counter missed injected ERROR");messages.queue->ClearStoredMessages();check(counts.errors.load()==1,"Clear reset runtime counter");sample("counter_control");
        const auto initialPrivate=privateBytes();
        auto held=texture(*device.Get(),0,counts);++created;auto alias=held;held.Reset();sample("alias_held");check(counts.resources.load()==0,"Early resource destruction");alias.Reset();sample("alias_released");check(counts.resources.load()==1,"Missing resource destruction");
        for(unsigned i=0;i<iterations;++i){for(unsigned j=0;j<3;++j){auto temporary=texture(*device.Get(),j,counts);++created;}
            ++done;check(counts.resources.load()==created,"Native texture destruction mismatch");
            if(done%64==0){sample("checkpoint");check(privateBytes()<=initialPrivate+512*MiB,"Runtime diagnostic private memory budget exceeded");}
        }
        sample("resources_released");check(counts.errors.load()==1,"Unexpected runtime validation ERROR/CORRUPTION");
    }
    configuration.Reset();device.Reset();const auto deadline=Clock::now()+std::chrono::seconds(5);
    while(counts.devices.load()==0&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    sample("device_released");check(counts.devices.load()==1,"Missing native device destruction");
    debug2.Reset();debug1.Reset();debug.Reset();factory.Reset();sdk.Reset();std::this_thread::sleep_for(std::chrono::milliseconds(200));sample("settled");
    out<<"{\"type\":\"footer\",\"functional_passed\":true,\"resource_creations\":"<<created<<",\"resource_destructions\":"<<counts.resources.load()<<",\"device_destructions\":"<<counts.devices.load()<<",\"validation_errors_total\":"<<counts.errors.load()<<",\"intentional_validation_errors\":1,\"unexpected_validation_errors\":"<<counts.errors.load()-1<<",\"validation_counter\":\"unfiltered_callback_before_teardown\",\"memory_acceptance_claim\":false}\n";out.flush();
}
}
int main(int argc,char** argv)try {
    check(argc==7,"Usage: gbv_runtime_probe global|inbox-factory|agility hardware-cpu|hardware-gbv|warp-gbv version runtime-path iterations output.jsonl");
    const std::string source=argv[1],mode=argv[2],path=argv[4];check(source=="global"||source=="inbox-factory"||source=="agility","Unknown runtime source");
    check(mode=="hardware-cpu"||mode=="hardware-gbv"||mode=="warp-gbv","Unknown runtime mode");const auto version=source=="agility"?positive(argv[3],999):0;
    check(source=="agility"?(std::filesystem::path(path).is_absolute()&&version>0):(std::string(argv[3])=="0"&&path=="-"),"Invalid runtime selection");
    const auto iterations=positive(argv[5],512);check(GetFileAttributesA(argv[6])==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND,"Refuse overwrite or invalid output path");
    std::ofstream out(argv[6]);check(out.good(),"Open runtime output");out.exceptions(std::ios::badbit|std::ios::failbit);out<<std::setprecision(12);compare(out,source,mode,version,path,iterations);
    std::cout<<"PASS runtime selection/callback/destruction contracts (not memory acceptance)\n";return 0;
} catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
