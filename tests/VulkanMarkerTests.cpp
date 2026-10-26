#include <graphics/Graphics.hpp>
#include "vulkan_marker/Observer.hpp"
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
unsigned assertions{};
void check(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
template<class T>void reject(gfx::Result<T> r,gfx::ErrorCode code){check(!r&&r.error().code==code,"Expected marker rejection");}
struct Observer {
#ifdef _WIN32
    HMODULE module{};
#else
    void* module{};
#endif
    markerprobe::Query query{};
    void open(const std::filesystem::path& path,bool deny){
#ifdef _WIN32
        module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        auto symbol=[&](const char* name){return module?GetProcAddress(module,name):nullptr;};
#else
        module=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
        auto symbol=[&](const char* name){return module?dlsym(module,name):nullptr;};
#endif
        check(module!=nullptr,"Observer module missing");
        query=reinterpret_cast<markerprobe::Query>(symbol("gfxMarkerSnapshot"));
        auto configure=reinterpret_cast<markerprobe::Configure>(symbol("gfxMarkerConfigure"));
        check(query&&configure,"Observer exports missing");configure(deny);
    }
    ~Observer(){
#ifdef _WIN32
        if(module)FreeLibrary(module);
#else
        if(module)dlclose(module);
#endif
    }
};
}
int main(int argc,char** argv)try{
    bool validation{},denied{};std::filesystem::path observerPath;
    for(int i=1;i<argc;++i){
        const std::string_view arg=argv[i];
        if(arg=="--validation"&&!validation)validation=true;
        else if(arg=="--deny-proc"&&!denied)denied=true;
        else if(arg=="--observe"&&i+1<argc&&observerPath.empty())observerPath=std::filesystem::absolute(argv[++i]);
        else throw std::runtime_error("Usage: [--validation] [--observe absolute-library] [--deny-proc]");
    }
    check(!denied||!observerPath.empty(),"Lookup denial requires observer");
    Observer observer;if(!observerPath.empty())observer.open(observerPath,denied);
    std::vector<std::string> expected;
    const auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));check(!adapters.empty(),"Real Vulkan adapter required");
    for(const auto& adapter:adapters){
        gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.validation=validation;desc.adapter=adapter.id;desc.softwareAdapter=adapter.softwareAdapter;
        auto device=take(gfx::createDevice(desc));auto command=take(device->createCommandList());
        reject(command->debugMarker("before"),gfx::ErrorCode::InvalidState);
        take(command->begin());
        bool rejected{};std::thread wrongThread([&]{auto r=command->debugMarker("wrong-thread");rejected=!r&&r.error().code==gfx::ErrorCode::InvalidState;});wrongThread.join();check(rejected,"Marker thread affinity");
        auto marker=[&](std::string_view name){auto result=command->debugMarker(name);if(denied)reject(std::move(result),gfx::ErrorCode::Unsupported);else{take(std::move(result));expected.emplace_back(name);}};
        const char substring[]="slice-not-terminated-here";marker(std::string_view(substring,5));
        std::string temporary="\xec\x9e\x84\xec\x8b\x9c-marker-\xf0\x9f\x99\x82";marker(temporary);temporary.assign(temporary.size(),'x');
        marker({});marker(std::string(200,'m'));
        const auto before=take(device->resourceStatistics());
        for(const std::string bad:{std::string("a\0b",3),std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xe2\x82",2),std::string("\x80",1)})
            reject(command->debugMarker(bad),denied?gfx::ErrorCode::Unsupported:gfx::ErrorCode::InvalidArgument);
        if constexpr(sizeof(std::size_t)>4)reject(command->debugMarker(std::string_view(substring,std::uint64_t(UINT32_MAX)+1)),denied?gfx::ErrorCode::Unsupported:gfx::ErrorCode::InvalidArgument);
        const auto after=take(device->resourceStatistics());
        check(before.uniqueResources==after.uniqueResources&&before.pendingObjectReferences==after.pendingObjectReferences,"Rejected marker preserves ownership");
        auto target=take(device->createTexture({17,9,gfx::Format::RGBA8UNorm,true}));
        take(command->transition(target,gfx::ResourceState::RenderTarget));take(command->beginRenderPass(target,{0,0,0,1}));marker("inside-pass");take(command->endRenderPass());
        take(command->end());reject(command->debugMarker("ended"),gfx::ErrorCode::InvalidState);
        const auto fence=take(device->queue().submit(*command));reject(command->debugMarker("submitted"),gfx::ErrorCode::InvalidState);take(device->queue().wait(fence));
        take(command->begin());marker("reused");take(command->end());take(device->queue().wait(take(device->queue().submit(*command))));
        command.reset();target.reset();take(device->queue().waitIdle());
        const auto stats=take(device->resourceStatistics());check(!stats.trackedObjects&&!stats.uniqueResources&&!stats.resourceAllocationBytes&&!stats.pendingSubmissions&&!stats.pendingObjectReferences,"Marker resources reclaimed");
        check(take(device->validationErrorCount())==0,"Marker validation errors");
        auto retired=take(device->createCommandList());take(retired->begin());device.reset();reject(retired->debugMarker("retired"),gfx::ErrorCode::DeviceLost);retired.reset();
        std::cout<<"{\"event\":\"adapter\",\"name\":"<<std::quoted(adapter.name)<<",\"adapter_id\":"<<adapter.id.value<<",\"software\":"<<std::boolalpha<<adapter.softwareAdapter<<",\"vendor\":"<<adapter.vendorId<<"}\n";
    }
    markerprobe::Snapshot snapshot;
    if(observer.query){
        check(observer.query(&snapshot,sizeof(snapshot))!=0,"Observer snapshot");
        std::cout<<"{\"event\":\"observer\",\"debug_instances\":"<<snapshot.debugInstances<<",\"lookups\":"<<snapshot.lookups<<",\"calls\":"<<snapshot.calls<<",\"forwarded\":"<<snapshot.forwarded<<"}\n";
        check(snapshot.debugInstances>0&&snapshot.lookups>0,"Observer actually loaded and debug extension enabled");
        check(!snapshot.errors&&!snapshot.duplicateExtensions&&!snapshot.liveInstances&&!snapshot.liveDevices,"Observer/instance/device lifetime");
        check(snapshot.calls==expected.size()&&snapshot.forwarded==expected.size(),"Exact native calls forwarded");
        for(unsigned i=0;i<snapshot.calls;++i){
            check(snapshot.labels[i].command&&expected[i]==snapshot.labels[i].text,"Exact native UTF-8 label and command handle");
            std::cout<<"{\"event\":\"native_label\",\"index\":"<<i<<",\"command\":"<<snapshot.labels[i].command<<",\"text\":"<<std::quoted(snapshot.labels[i].text)<<"}\n";
        }
    }
    std::cout<<"{\"event\":\"result\",\"source\":"<<std::quoted(gfx::buildFingerprint())<<",\"validation\":"<<std::boolalpha<<validation<<",\"denied_proc\":"<<denied
        <<",\"observed\":"<<(observer.query!=nullptr)<<",\"assertions\":"<<assertions<<",\"adapters\":"<<adapters.size()<<",\"accepted_markers\":"<<expected.size()
        <<",\"native_calls\":"<<snapshot.calls<<",\"forwarded\":"<<snapshot.forwarded<<",\"observer_errors\":"<<snapshot.errors<<",\"live_instances\":"<<snapshot.liveInstances<<",\"live_devices\":"<<snapshot.liveDevices<<"}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
