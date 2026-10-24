#include "Client.hpp"
#include "../../benchmarks/common/Adapter.hpp"
#include "../../benchmarks/vulkan_upload/Identity.hpp"
#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <span>

#ifndef NDEBUG
#error Release required
#endif
namespace {
unsigned checks{},verified{};std::uint64_t bytes{};
void check(bool b){++checks;if(!b)throw std::runtime_error("Fault GPU contract");}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
struct Case {const char* name;unsigned guards;int a,b,bind;unsigned dirty;gfx::BufferDesc desc{4096};bool success{},held{};};
constexpr std::array cases{
    Case{"control",0,0,0,0,0,{4096},true},
    Case{"pool-host",1,-1,0,0,1,{4096},true},Case{"pool-device",1,-2,0,0,1,{4096},true},
    Case{"double-host",1,-1,-1,0,1},Case{"double-device",1,-2,-2,0,1},Case{"mixed-oom",1,-1,-2,0,1},
    Case{"pool-unknown",1,-13,0,0,1},Case{"bind-fresh",0,0,0,-2,0},Case{"bind-grow",1,0,0,-1,0},
    Case{"bind-reuse",2,0,0,-2,0},Case{"fallback-bind",1,-2,0,-1,1},
    Case{"dedicated-oom",1,-2,0,0,1,{8192}},Case{"upload-oom",1,-1,0,0,1,{4096,gfx::Memory::Upload}},
    Case{"readback-oom",1,-2,0,0,1,{4096,gfx::Memory::Readback}},Case{"held-bind",2,0,0,-2,0,{4096},false,true}
};
std::vector<std::byte> payload(std::uint64_t size,unsigned seed){std::vector<std::byte> p(static_cast<std::size_t>(size));for(std::size_t i=0;i<p.size();++i)p[i]=std::byte((i*37+seed*53)&255);return p;}
void copyCheck(gfx::Device&d,const gfx::BufferPtr&b,unsigned seed,bool initialize){
    const auto desc=b->description();auto expected=payload(desc.size,seed);
    gfx::BufferPtr up,down;
    if(initialize&&desc.memory!=gfx::Memory::Upload){up=take(d.createBuffer({desc.size,gfx::Memory::Upload}));take(up->write(0,expected));}
    if(desc.memory!=gfx::Memory::Readback)down=take(d.createBuffer({desc.size,gfx::Memory::Readback}));
    if(initialize&&desc.memory==gfx::Memory::Upload)take(b->write(0,expected));
    auto cmd=take(d.createCommandList());take(cmd->begin());
    if(up){if(desc.memory==gfx::Memory::Device)take(cmd->transition(b,gfx::ResourceState::CopyDestination));take(cmd->copyBuffer(up,b,desc.size));}
    if(down){if(desc.memory==gfx::Memory::Device)take(cmd->transition(b,gfx::ResourceState::CopySource));take(cmd->copyBuffer(b,down,desc.size));}
    take(cmd->end());take(d.queue().wait(take(d.queue().submit(*cmd))));
    check(take((down?down:b)->read(0,desc.size))==expected);++verified;bytes+=desc.size;
}
bool empty(const gfx::ResourceStatistics&s){return !s.trackedObjects&&!s.uniqueResources&&!s.resourceAllocationBytes&&!s.pendingSubmissions&&!s.pendingObjectReferences;}
std::string validationLayer(bool enabled){
    auto module=GetModuleHandleW(L"VkLayer_khronos_validation.dll");if(!enabled){check(!module);return{};}
    wchar_t actual[32768]{},expected[32768]{};
    auto n=module?GetModuleFileNameW(module,actual,32768):0;auto m=GetEnvironmentVariableW(L"GFX_EXPECT_VULKAN_LAYER",expected,32768);
    check(n&&n<32768&&m&&m<32768&&std::filesystem::equivalent(actual,expected));return std::filesystem::path(actual).generic_string();
}
}
int main(int argc,char**argv)try{
    using bench::quote;
    if(argc==2&&std::string_view(argv[1])=="--probe"){
        auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));std::cout<<"{\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"adapters\":[";bool comma{};
        for(auto&a:adapters)if(!a.softwareAdapter){if(comma)std::cout<<',';comma=true;std::cout<<quote(a.name);}std::cout<<"]}\n";return 0;
    }
    if(argc!=4||(std::string_view(argv[3])!="0"&&std::string_view(argv[3])!="1"))throw std::runtime_error("Expected CASE exact-GPU validation-0-or-1");
    const auto it=std::find_if(cases.begin(),cases.end(),[&](auto&c){return c.name==std::string_view(argv[1]);});if(it==cases.end())throw std::runtime_error("Unknown fault case");const auto c=*it;
    const bool validation=std::string_view(argv[3])=="1";
    auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));auto a=std::find_if(adapters.begin(),adapters.end(),[&](auto&x){return x.name==argv[2]&&!x.softwareAdapter;});
    if(a==adapters.end()||std::count_if(adapters.begin(),adapters.end(),[&](auto&x){return x.name==argv[2];})!=1)throw std::runtime_error("Unique exact GPU required");
    const auto identity=upload::identify(a->name);
    gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=a->id;desc.validation=validation;
    auto d=take(gfx::createDevice(desc));traceclient::Client fault;auto layer=validationLayer(validation);
    for(unsigned iteration=0;iteration<3;++iteration){
        fault.set(1,iteration);std::vector<gfx::BufferPtr> guards;
        for(unsigned i=0;i<c.guards;++i){auto b=take(d->createBuffer({4096}));copyCheck(*d,b,iteration*9+i+1,true);guards.push_back(std::move(b));}
        std::unique_ptr<gfx::CommandList> held;gfx::BufferPtr heldRead;
        if(c.held){heldRead=take(d->createBuffer({4096,gfx::Memory::Readback}));held=take(d->createCommandList());take(held->begin());take(held->copyBuffer(guards.back(),heldRead,4096));take(held->end());guards.back().reset();}
        auto before=take(d->resourceStatistics());fault.set(2,iteration);check(fault.arm(c.a,c.b,c.bind,c.dirty)!=0);
        auto result=d->createBuffer(c.desc);check(bool(result)==c.success);
        if(!result){int code=c.bind?c.bind:c.b?c.b:c.a;check(result.error().nativeCode==code);check(result.error().code==(code==-13?gfx::ErrorCode::BackendFailure:gfx::ErrorCode::OutOfMemory));}
        auto after=take(d->resourceStatistics());
        if(!result){check(after.trackedObjects==before.trackedObjects&&after.uniqueResources==before.uniqueResources&&after.resourceAllocationBytes==before.resourceAllocationBytes&&after.pendingSubmissions==before.pendingSubmissions&&after.pendingObjectReferences==before.pendingObjectReferences);}
        else check(after.uniqueResources==before.uniqueResources+1);
        check((fault.status()&6u)==0);fault.set(3,iteration);take(d->checkStatus());
        auto target=result?std::move(*result):take(d->createBuffer(c.desc));copyCheck(*d,target,iteration*9+7,true);
        for(unsigned i=0;i<guards.size();++i)if(guards[i])copyCheck(*d,guards[i],iteration*9+i+1,false);
        if(held){take(d->queue().wait(take(d->queue().submit(*held))));check(take(heldRead->read(0,4096))==payload(4096,iteration*9+c.guards));++verified;bytes+=4096;}
        fault.set(4,iteration);target.reset();guards.clear();held.reset();heldRead.reset();take(d->queue().waitIdle());check(empty(take(d->resourceStatistics())));
        fault.set(5,iteration);check(empty(take(d->resourceStatistics())));fault.set(0,iteration);
    }
    if(validation)check(take(d->validationErrorCount())==0);
    fault.set(6,3);d.reset();fault.set(0,3);auto trace=fault.result();unsigned status=fault.status();
    std::cout<<"{\"schema_version\":1,\"contract\":\"vulkan_buffer_fault_v1\",\"case\":"<<quote(c.name)<<",\"device_name\":"<<quote(a->name)<<",\"validation\":"<<(validation?"true":"false")<<",\"loaded_validation_layer\":"<<quote(layer)<<",\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"source_fingerprint\":"<<quote(GFX_TRACE_FINGERPRINT)<<",\"device_uuid\":"<<quote(identity.uuid)<<",\"driver_uuid\":"<<quote(identity.driverUuid)<<",\"device_luid\":"<<quote(identity.luid)<<",\"environment\":"<<bench::environment(a->name)<<",\"repetitions\":3,\"assertions\":"<<checks<<",\"verified_buffers\":"<<verified<<",\"verified_bytes\":"<<bytes<<",\"regression_detected\":"<<(status?"true":"false")<<",\"native_trace\":"<<trace<<"}\n";
    return status?2:0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
