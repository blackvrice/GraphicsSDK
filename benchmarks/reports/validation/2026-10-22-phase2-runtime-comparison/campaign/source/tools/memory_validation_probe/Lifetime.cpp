// Standalone native reproduction: no GraphicsSDK headers, objects or library.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
constexpr std::uint64_t MiB = 1024*1024;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void hr(HRESULT value) { if (FAILED(value)) throw std::runtime_error("Native HRESULT="+std::to_string(value)); }
std::string utf8(const wchar_t* value) {
    const int count=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    check(count>0,"UTF-8 conversion");std::string result(count,'\0');
    check(WideCharToMultiByte(CP_UTF8,0,value,-1,result.data(),count,nullptr,nullptr)>0,"UTF-8 conversion");result.pop_back();return result;
}
std::string quote(std::string_view value) {
    std::string result="\"";
    for(unsigned char c:value){check(c>=32,"JSON control character");if(c=='"'||c=='\\')result+='\\';result+=char(c);}
    return result+'"';
}
struct Counts {
    std::atomic<unsigned> resources{},devices{},errors{};
    static void CALLBACK resource(void* context) { static_cast<Counts*>(context)->resources.fetch_add(1); }
    static void CALLBACK device(void* context) { static_cast<Counts*>(context)->devices.fetch_add(1); }
    static void CALLBACK message(D3D12_MESSAGE_CATEGORY,D3D12_MESSAGE_SEVERITY severity,D3D12_MESSAGE_ID,LPCSTR,void* context) {
        if(severity==D3D12_MESSAGE_SEVERITY_ERROR||severity==D3D12_MESSAGE_SEVERITY_CORRUPTION)static_cast<Counts*>(context)->errors.fetch_add(1);
    }
};
void watchResource(ID3D12Resource& resource,Counts& counts) {
    ComPtr<ID3DDestructionNotifier> notifier;hr(resource.QueryInterface(IID_PPV_ARGS(&notifier)));UINT cookie{};
    hr(notifier->RegisterDestructionCallback(Counts::resource,&counts,&cookie));
    // The notifier must not keep an extra interface alive beyond this call.
}
struct Messages {
    ComPtr<ID3D12InfoQueue1> queue;DWORD cookie{};
    Messages(ID3D12Device& device,Counts& counts){hr(device.QueryInterface(IID_PPV_ARGS(&queue)));hr(queue->RegisterMessageCallback(Counts::message,D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,&counts,&cookie));}
    ~Messages(){if(queue)(void)queue->UnregisterMessageCallback(cookie);}
};
D3D12_RESOURCE_DESC textureDesc(unsigned index) {
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=d.Height=64;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;
    d.Format=index==1?DXGI_FORMAT_D32_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Flags=index==0?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:index==1?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL|D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE:D3D12_RESOURCE_FLAG_NONE;
    return d;
}
ComPtr<ID3D12Resource> texture(ID3D12Device& device,unsigned index,Counts& counts) {
    auto d=textureDesc(index);D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> resource;
    hr(device.CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource)));watchResource(*resource.Get(),counts);return resource;
}
std::uint64_t privateBytes() {
    PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof(p);check(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p))!=0,"Private commit query");return p.PrivateUsage;
}
void modules(std::ostream& out) {
    out<<"[";bool first=true;
    for(const auto* name:{L"d3d12.dll",L"D3D12Core.dll",L"d3d12SDKLayers.dll",L"d3d10warp.dll"}) {
        auto module=GetModuleHandleW(name);if(!module)continue;wchar_t path[32768]{};
        auto count=GetModuleFileNameW(module,path,32768);check(count>0&&count<32768,"Runtime module path");
        if(!first)out<<",";first=false;out<<quote(utf8(path));
    }out<<"]";
}
void run(std::ostream& out,std::string_view mode,unsigned cycles,unsigned iterations) {
    const bool warp=mode=="warp-gbv",gpu=mode!="hardware-cpu";
    ComPtr<ID3D12Debug> debug;hr(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();ComPtr<ID3D12Debug1> debug1;hr(debug.As(&debug1));debug1->SetEnableGPUBasedValidation(gpu);
    ComPtr<ID3D12Debug2> debug2;hr(debug.As(&debug2));debug2->SetGPUBasedValidationFlags(D3D12_GPU_BASED_VALIDATION_FLAGS_NONE);
    ComPtr<IDXGIFactory4> factory;hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> selected;
    if(warp)hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&selected)));
    else for(UINT i=0;;++i){hr(factory->EnumAdapters1(i,&selected));DXGI_ADAPTER_DESC1 d{};hr(selected->GetDesc1(&d));if(!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))break;selected.Reset();}
    DXGI_ADAPTER_DESC1 desc{};hr(selected->GetDesc1(&desc));ComPtr<IDXGIAdapter3> observer;hr(selected.As(&observer));
    LARGE_INTEGER driver{};const auto driverStatus=selected->CheckInterfaceSupport(__uuidof(IDXGIDevice),&driver);
    out<<"{\"type\":\"header\",\"schema\":1,\"mode\":"<<quote(mode)<<",\"native_only\":true,\"gpu_validation\":"<<(gpu?"true":"false")<<",\"gpu_state_tracking\":true,\"cycles\":"<<cycles<<",\"iterations_per_cycle\":"<<iterations<<",\"adapter\":"<<quote(utf8(desc.Description))<<",\"software\":"<<(warp?"true":"false")<<",\"luid_high\":"<<desc.AdapterLuid.HighPart<<",\"luid_low\":"<<desc.AdapterLuid.LowPart<<",\"driver_query_hresult\":"<<driverStatus<<",\"driver_version\":"<<driver.QuadPart<<"}\n";
    // Keep callback storage valid even when a failed destruction check finds a live object.
    static Counts counts;
    const auto start=Clock::now();unsigned created=0;const auto initialPrivate=privateBytes();
    auto sample=[&](std::string_view phase,unsigned cycle) {
        DWORD handles{};check(GetProcessHandleCount(GetCurrentProcess(),&handles)!=0,"Handle count");DXGI_QUERY_VIDEO_MEMORY_INFO local{},nonlocal{};
        const auto a=observer->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local),b=observer->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&nonlocal);
        out<<"{\"type\":\"sample\",\"phase\":"<<quote(phase)<<",\"cycle\":"<<cycle<<",\"elapsed\":"<<std::chrono::duration<double>(Clock::now()-start).count()<<",\"resource_creations\":"<<created<<",\"resource_destructions\":"<<counts.resources.load()<<",\"device_destructions\":"<<counts.devices.load()<<",\"validation_errors\":"<<counts.errors.load()<<",\"private_bytes\":"<<privateBytes()<<",\"handles\":"<<handles<<",\"gpu_local_hresult\":"<<a<<",\"gpu_nonlocal_hresult\":"<<b<<",\"gpu_local_bytes\":"<<(SUCCEEDED(a)?std::to_string(local.CurrentUsage):"null")<<",\"gpu_nonlocal_bytes\":"<<(SUCCEEDED(b)?std::to_string(nonlocal.CurrentUsage):"null")<<"}\n";out.flush();
    };
    sample("baseline",0);
    for(unsigned cycle=1;cycle<=cycles;++cycle) {
        ComPtr<ID3D12Device> device;hr(D3D12CreateDevice(selected.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        {ComPtr<ID3DDestructionNotifier> notifier;hr(device.As(&notifier));UINT cookie{};hr(notifier->RegisterDestructionCallback(Counts::device,&counts,&cookie));}
        {
            Messages messages(*device.Get(),counts);
            sample("device_ready",cycle);
            const auto before=counts.resources.load();
            auto resource=texture(*device.Get(),0,counts);++created;auto alias=resource;
            resource.Reset();sample("alias_held",cycle);check(counts.resources.load()==before,"Resource destroyed before last alias");
            alias.Reset();sample("alias_released",cycle);check(counts.resources.load()==before+1,"Missing last-alias destruction callback");
            for(unsigned i=0;i<iterations;++i)for(unsigned j=0;j<3;++j){auto temporary=texture(*device.Get(),j,counts);++created;}
            sample("resources_released",cycle);
            check(counts.resources.load()==created,"Native resources retained after last release");
            check(counts.errors.load()==0,"Native validation error");
            out<<"{\"type\":\"runtime_modules\",\"cycle\":"<<cycle<<",\"paths\":";modules(out);out<<"}\n";out.flush();
        }
        device.Reset();
        // Bound the wait for asynchronous destruction; never infer it from wrapper counts.
        const auto deadline=Clock::now()+std::chrono::seconds(5);
        while(counts.devices.load()<cycle&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sample("device_released",cycle);check(counts.devices.load()==cycle,"Missing native device destruction callback");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));sample("settled",cycle);
        check(privateBytes()<=initialPrivate+512*MiB,"Diagnostic memory safety budget exceeded");
    }
    out<<"{\"type\":\"footer\",\"lifetime_contracts_passed\":true,\"cycles\":"<<cycles<<",\"resource_creations\":"<<created<<",\"resource_destructions\":"<<counts.resources.load()<<",\"device_destructions\":"<<counts.devices.load()<<",\"validation_errors\":"<<counts.errors.load()<<",\"memory_acceptance_claim\":false}\n";out.flush();
}
unsigned positive(const char* input,unsigned maximum) {
    std::size_t used{};const std::string value=input;auto n=std::stoul(value,&used);check(used==value.size()&&n>0&&n<=maximum,"Invalid count");return static_cast<unsigned>(n);
}
}
int main(int argc,char** argv)try {
    check(argc==5,"Usage: gbv_lifetime_probe hardware-cpu|hardware-gbv|warp-gbv cycles iterations output.jsonl");const std::string mode=argv[1];
    check(mode=="hardware-cpu"||mode=="hardware-gbv"||mode=="warp-gbv","Unknown lifetime mode");const auto cycles=positive(argv[2],16),iterations=positive(argv[3],512);
    check(GetFileAttributesA(argv[4])==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND,"Refuse overwrite or invalid output path");
    std::ofstream out(argv[4]);check(out.good(),"Open output");out.exceptions(std::ios::badbit|std::ios::failbit);out<<std::setprecision(12);run(out,mode,cycles,iterations);
    std::cout<<"PASS native destruction callbacks (not memory acceptance)\n";return 0;
} catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
