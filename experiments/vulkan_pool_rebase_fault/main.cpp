#include "../vulkan_pool_churn/Snapshot.hpp"
#include "../vulkan_buffer_pool_fault/Client.hpp"
#include "../../benchmarks/common/Adapter.hpp"
#include "../../benchmarks/vulkan_upload/Identity.hpp"
#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <span>
#ifndef NDEBUG
#error Release required
#endif
namespace {
using churn::Snapshot;
unsigned checks{},verified{};std::uint64_t bytes{};
void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
struct Case{const char*name;int a,b,bind;bool success,reuse,held;};
constexpr std::array cases{
    Case{"mixed-host",-1,0,0,true,false,false},Case{"mixed-device",-2,0,0,true,false,false},
    Case{"mixed-double",-1,-2,0,false,false,false},Case{"mixed-unknown",-13,0,0,false,false,false},
    Case{"held-fallback-bind",-2,0,-1,false,false,true},
    Case{"reuse-then-host",-1,0,0,true,true,false},Case{"reuse-then-device",-2,0,0,true,true,false}
};
struct Client:traceclient::Client{
    using Read=int(__cdecl*)(Snapshot*,unsigned);Read read{};
    Client(){read=reinterpret_cast<Read>(GetProcAddress(module,"gfxChurnSnapshot"));check(read!=nullptr,"Read-only complete bindings export");}
    auto snapshot(){auto s=std::make_unique<Snapshot>();check(read(s.get(),sizeof(*s))!=0,"Complete bounded native bindings");return s;}
};
std::vector<std::byte> payload(unsigned size,unsigned seed){std::vector<std::byte> p(size);for(unsigned i=0;i<size;++i)p[i]=std::byte((i*37+seed*53)&255);return p;}
void copyCheck(gfx::Device&d,const gfx::BufferPtr&b,unsigned seed,bool init){
    unsigned size=static_cast<unsigned>(b->description().size);auto expected=payload(size,seed);gfx::BufferPtr up;
    if(init){up=take(d.createBuffer({size,gfx::Memory::Upload}));take(up->write(0,expected));}
    auto down=take(d.createBuffer({size,gfx::Memory::Readback}));auto cmd=take(d.createCommandList());take(cmd->begin());
    if(up){take(cmd->transition(b,gfx::ResourceState::CopyDestination));take(cmd->copyBuffer(up,b,size));}
    take(cmd->transition(b,gfx::ResourceState::CopySource));take(cmd->copyBuffer(b,down,size));take(cmd->end());take(d.queue().wait(take(d.queue().submit(*cmd))));
    check(take(down->read(0,size))==expected,"Full distinct buffer data");++verified;bytes+=size;
}
void unchanged(const Snapshot&a,const Snapshot&b){
    check(a.memories==b.memories&&a.bytes==b.bytes&&a.buffers==b.buffers&&a.images==b.images&&a.count==b.count,"Failed create preserves native ownership");
    for(unsigned i=0;i<a.count;++i)check(a.entries[i]==b.entries[i],"Failed create preserves native bindings");
}
churn::Entry added(const Snapshot&a,const Snapshot&b){
    churn::Entry e{};unsigned found{};
    for(unsigned i=0;i<b.count;++i)if(std::none_of(a.entries,a.entries+a.count,[&](auto&x){return x.buffer==b.entries[i].buffer;})){e=b.entries[i];++found;}
    check(found==1,"One new successful target binding");return e;
}
void emit(const Snapshot&s){
    std::cout<<"{\"memories\":"<<s.memories<<",\"bytes\":"<<s.bytes<<",\"buffers\":"<<s.buffers<<",\"images\":"<<s.images<<",\"bindings\":[";
    for(unsigned i=0;i<s.count;++i){if(i)std::cout<<',';auto&e=s.entries[i];std::cout<<"{\"buffer\":"<<e.buffer<<",\"memory\":"<<e.memory<<",\"offset\":"<<e.offset<<",\"required\":"<<e.required<<",\"alignment\":"<<e.alignment<<",\"allocation\":"<<e.allocation<<",\"type\":"<<e.type<<",\"type_bits\":"<<e.typeBits<<",\"dedicated\":"<<e.dedicated<<'}';}std::cout<<"]}";
}
bool empty(const gfx::ResourceStatistics&s){return !s.trackedObjects&&!s.uniqueResources&&!s.resourceAllocationBytes&&!s.pendingSubmissions&&!s.pendingObjectReferences;}
std::string validationLayer(bool enabled){auto m=GetModuleHandleW(L"VkLayer_khronos_validation.dll");if(!enabled){check(!m,"Unexpected VVL");return{};}wchar_t actual[32768]{},expected[32768]{};auto n=m?GetModuleFileNameW(m,actual,32768):0;auto e=GetEnvironmentVariableW(L"GFX_EXPECT_VULKAN_LAYER",expected,32768);check(n&&n<32768&&e&&e<32768&&std::filesystem::equivalent(actual,expected),"Pinned VVL path");return std::filesystem::path(actual).generic_string();}
}
int main(int argc,char**argv)try{
    using bench::quote;
    if(argc!=4||(std::string_view(argv[3])!="0"&&std::string_view(argv[3])!="1"))throw std::runtime_error("Expected mixed-context CASE exact-GPU validation-0/1");
    auto it=std::find_if(cases.begin(),cases.end(),[&](auto&c){return c.name==std::string_view(argv[1]);});if(it==cases.end())throw std::runtime_error("Unknown mixed-context case");auto c=*it;
    bool validation=std::string_view(argv[3])=="1";auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));auto a=std::find_if(adapters.begin(),adapters.end(),[&](auto&x){return x.name==argv[2]&&!x.softwareAdapter;});
    if(a==adapters.end()||std::count_if(adapters.begin(),adapters.end(),[&](auto&x){return x.name==argv[2];})!=1)throw std::runtime_error("Unique exact hardware GPU required");auto identity=upload::identify(a->name);
    gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=a->id;desc.validation=validation;auto d=take(gfx::createDevice(desc));Client fault;auto layer=validationLayer(validation);
    struct Cycle{std::unique_ptr<Snapshot> before,reused,failed,retry,clean;unsigned pendingAfterReuse{};};std::array<Cycle,3> cycles;
    for(unsigned i=0;i<cycles.size();++i){
        fault.set(1,i);std::vector<gfx::BufferPtr> guards;
        for(unsigned size:{4096u,64u,8192u}){auto b=take(d->createBuffer({size}));copyCheck(*d,b,i*10+unsigned(guards.size())+1,true);guards.push_back(b);}
        if(c.reuse){auto b=take(d->createBuffer({4096}));copyCheck(*d,b,i*10+4,true);guards.push_back(b);}
        std::unique_ptr<gfx::CommandList> held;gfx::BufferPtr heldRead;
        if(c.held){heldRead=take(d->createBuffer({4096,gfx::Memory::Readback}));held=take(d->createCommandList());take(held->begin());take(held->copyBuffer(guards[0],heldRead,4096));take(held->end());std::weak_ptr<gfx::Buffer> w=guards[0];guards[0].reset();check(w.expired(),"Recording retains failed-growth guard allocation");}
        auto publicBefore=take(d->resourceStatistics());auto&cycle=cycles[i];cycle.before=fault.snapshot();fault.set(2,i);check(fault.arm(c.a,c.b,c.bind,1)!=0,"Arm guarded poisoned allocation errors");
        gfx::BufferPtr reused;
        if(c.reuse){reused=take(d->createBuffer({4096}));cycle.reused=fault.snapshot();cycle.pendingAfterReuse=fault.status();check(cycle.pendingAfterReuse==4,"Hole reuse does not consume allocation fault");auto e=added(*cycle.before,*cycle.reused);check(e.offset==4096&&e.allocation==8192&&cycle.reused->memories==cycle.before->memories&&cycle.reused->bytes==cycle.before->bytes,"Reuse existing second block before growth");}
        else cycle.reused=fault.snapshot();
        auto result=d->createBuffer({4096});check(bool(result)==c.success,"Planned target success/failure");
        if(!result){int code=c.bind?c.bind:c.b?c.b:c.a;check(result.error().nativeCode==code&&result.error().code==(code==-13?gfx::ErrorCode::BackendFailure:gfx::ErrorCode::OutOfMemory),"Exact failure code");}
        cycle.failed=fault.snapshot();check(!fault.status(),"No pending faults/invalid frees/unsafe binds");
        if(!result){unchanged(*cycle.before,*cycle.failed);auto after=take(d->resourceStatistics());check(after.uniqueResources==publicBefore.uniqueResources&&after.resourceAllocationBytes==publicBefore.resourceAllocationBytes&&after.trackedObjects==publicBefore.trackedObjects&&after.pendingSubmissions==publicBefore.pendingSubmissions&&after.pendingObjectReferences==publicBefore.pendingObjectReferences,"Failed growth public rollback");}
        else{auto e=added(*cycle.reused,*cycle.failed);check(e.offset==0&&e.required==4096&&e.allocation==4096,"OOM falls back to exact standalone 4KiB");}
        fault.set(3,i);auto target=result?std::move(*result):take(d->createBuffer({4096}));copyCheck(*d,target,i*10+7,true);if(reused)copyCheck(*d,reused,i*10+8,true);
        for(unsigned j=0;j<guards.size();++j)if(guards[j])copyCheck(*d,guards[j],i*10+j+1,false);
        if(held){take(d->queue().wait(take(d->queue().submit(*held))));check(take(heldRead->read(0,4096))==payload(4096,i*10+1),"Held guard data survives failed fallback binding");++verified;bytes+=4096;}
        cycle.retry=fault.snapshot();if(!c.success){auto e=added(*cycle.before,*cycle.retry);check(e.offset==0&&e.allocation==8192,"Failed growth leaves the retry policy unchanged");}
        fault.set(4,i);target.reset();reused.reset();guards.clear();held.reset();heldRead.reset();take(d->queue().waitIdle());check(empty(take(d->resourceStatistics())),"All public ownership released");cycle.clean=fault.snapshot();check(!cycle.clean->memories&&!cycle.clean->bytes&&!cycle.clean->buffers&&!cycle.clean->images,"All native ownership released");fault.set(5,i);fault.set(0,i);
    }
    if(validation)check(take(d->validationErrorCount())==0,"No VVL errors");fault.set(6,3);d.reset();fault.set(0,3);auto final=fault.snapshot();check(!final->buffers&&!final->memories&&!final->bytes&&!fault.status(),"Teardown is explicit and empty");
    std::cout<<"{\"schema_version\":1,\"contract\":\"vulkan_mixed_fallback_v1\",\"case\":"<<quote(c.name)<<",\"validation\":"<<(validation?"true":"false")<<",\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"source_fingerprint\":"<<quote(GFX_TRACE_FINGERPRINT)<<",\"device_name\":"<<quote(a->name)<<",\"device_uuid\":"<<quote(identity.uuid)<<",\"driver_uuid\":"<<quote(identity.driverUuid)<<",\"device_luid\":"<<quote(identity.luid)<<",\"environment\":"<<bench::environment(a->name)<<",\"loaded_validation_layer\":"<<quote(layer)<<",\"assertions\":"<<checks<<",\"verified_buffers\":"<<verified<<",\"verified_bytes\":"<<bytes<<",\"cycles\":[";
    for(unsigned i=0;i<cycles.size();++i){if(i)std::cout<<',';auto&x=cycles[i];std::cout<<"{\"before\":";emit(*x.before);std::cout<<",\"reused\":";emit(*x.reused);std::cout<<",\"after_target\":";emit(*x.failed);std::cout<<",\"retry\":";emit(*x.retry);std::cout<<",\"clean\":";emit(*x.clean);std::cout<<",\"pending_after_reuse\":"<<x.pendingAfterReuse<<'}';}
    std::cout<<"],\"native_trace\":"<<fault.result()<<"}\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
