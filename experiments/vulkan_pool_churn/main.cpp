#include "Model.hpp"
#include "../../experiments/vulkan_buffer_pool_fault/Client.hpp"
#include "../../benchmarks/common/Adapter.hpp"
#include "../../benchmarks/vulkan_upload/Identity.hpp"
#include <graphics/Graphics.hpp>
#include <array>
#include <iostream>
#include <span>
#ifndef NDEBUG
#error Release required
#endif
namespace {
using namespace churn;
unsigned assertions{},verified{};std::uint64_t verifiedBytes{};
void check(bool b,const char*m){++assertions;require(b,m);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
constexpr std::array sizes{1u,63u,64u,65u,127u,128u,129u,255u,256u,257u,511u,512u,513u,1023u,1024u,1025u,2047u,2048u,2049u,4095u,4096u,4097u,8192u,65537u};
struct Item{gfx::BufferPtr buffer;unsigned size{},seed{};std::uint64_t native{};};
struct Row{std::string label;unsigned round{};std::uint64_t buffers{},allocations{},reserved{},required{},payload{},reuses{},growths{};};
struct Context {
    gfx::Device& d;traceclient::Client fault;using Read=int(__cdecl*)(Snapshot*,unsigned);Read read{};Model model;std::vector<Row> rows;std::set<unsigned> classes;std::map<std::uint64_t,unsigned> logicalSizes;
    Context(gfx::Device&device,bool candidate):d(device),model(candidate){read=reinterpret_cast<Read>(GetProcAddress(fault.module,"gfxChurnSnapshot"));check(read!=nullptr,"Read-only binding snapshot export");}
    auto snapshot(){auto s=std::make_unique<Snapshot>();check(read(s.get(),sizeof(*s))!=0,"Bounded complete native snapshot");return s;}
    Item make(unsigned size,unsigned seed,gfx::Memory memory=gfx::Memory::Device){
        auto b=take(d.createBuffer({size,memory}));auto s=snapshot();Entry found{};unsigned count{};
        for(unsigned i=0;i<s->count;++i)if(!model.live.contains(s->entries[i].buffer)){found=s->entries[i];++count;}
        check(count==1,"One new buffer binding per SDK create");model.add(found,memory==gfx::Memory::Device);model.verify(*s);logicalSizes.emplace(found.buffer,size);
        if(auto stride=churn::stride(found,memory==gfx::Memory::Device))classes.insert(stride);
        return {std::move(b),size,seed,found.buffer};
    }
    void drop(Item&i){i.buffer.reset();model.remove(i.native);logicalSizes.erase(i.native);i.native=0;}
    void capture(const char*label,unsigned round,bool stats=true){
        auto s=snapshot();model.verify(*s);std::uint64_t required{},payload{};for(unsigned i=0;i<s->count;++i){required+=s->entries[i].required;payload+=logicalSizes.at(s->entries[i].buffer);}
        if(stats){auto p=take(d.resourceStatistics());check(p.uniqueResources==s->buffers&&p.resourceAllocationBytes==s->bytes,"Public counts equal unique native ownership");check(!p.pendingSubmissions&&!p.pendingObjectReferences,"No pending work at stable checkpoint");}
        rows.push_back({label,round,s->buffers,s->memories,s->bytes,required,payload,model.reuses,model.growths});
    }
    void inject(unsigned round){
        auto before=snapshot();auto publicBefore=take(d.resourceStatistics());int code=round%2?-1:-2;
        check(fault.arm(0,0,code,0)!=0,"Arm synthetic bind failure");auto r=d.createBuffer({4096});
        check(!r&&r.error().nativeCode==code&&r.error().code==gfx::ErrorCode::OutOfMemory,"Bind error preserved");check(!fault.status(),"Injected error consumed without invalid cleanup");
        auto after=snapshot();model.verify(*after);check(before->count==after->count&&before->memories==after->memories&&before->bytes==after->bytes,"Failed binding leaves no slot/block");
        auto publicAfter=take(d.resourceStatistics());check(publicBefore.uniqueResources==publicAfter.uniqueResources&&publicBefore.resourceAllocationBytes==publicAfter.resourceAllocationBytes&&publicBefore.trackedObjects==publicAfter.trackedObjects,"Failed create preserves public ownership");
    }
};
std::vector<std::byte> payload(unsigned size,unsigned seed){std::vector<std::byte> p(size);for(unsigned i=0;i<size;++i)p[i]=std::byte((i*37u+seed*53u+(i>>8)*17u)&255);return p;}
void verifyAll(Context&c,const std::vector<Item>&items,bool initialize){
    std::vector<gfx::BufferPtr> up,down;auto cmd=take(c.d.createCommandList());take(cmd->begin());
    for(auto&i:items){check(i.buffer!=nullptr,"Live wrapper during data check");
        if(initialize){auto u=take(c.d.createBuffer({i.size,gfx::Memory::Upload}));take(u->write(0,payload(i.size,i.seed)));take(cmd->transition(i.buffer,gfx::ResourceState::CopyDestination));take(cmd->copyBuffer(u,i.buffer,i.size));up.push_back(u);}
        auto r=take(c.d.createBuffer({i.size,gfx::Memory::Readback}));take(cmd->transition(i.buffer,gfx::ResourceState::CopySource));take(cmd->copyBuffer(i.buffer,r,i.size));down.push_back(r);
    }
    take(cmd->end());take(c.d.queue().wait(take(c.d.queue().submit(*cmd))));
    for(unsigned i=0;i<items.size();++i){check(take(down[i]->read(0,items[i].size))==payload(items[i].size,items[i].seed),"Full distinct GPU buffer contents");++verified;verifiedBytes+=items[i].size;}
}
void clear(Context&c,std::vector<Item>&items){for(auto&i:items)c.drop(i);items.clear();}
void mixed(Context&c){
    std::vector<Item> live;
    for(unsigned round=0;round<16;++round){
        c.fault.set(1,round);std::vector<Item> created;
        while(live.size()<96){auto index=unsigned(live.size());auto i=c.make(sizes[(index+round*7)%sizes.size()],1000+round*101+index);created.push_back(i);live.push_back(i);}
        verifyAll(c,created,true);created.clear();c.capture("full",round);
        c.fault.set(2,round);auto divisor=2u+round%3;std::vector<Item> survivors;
        for(unsigned i=0;i<live.size();++i){if((i+round)%divisor==0)survivors.push_back(live[i]);else c.drop(live[i]);}live.clear();live=std::move(survivors);
        c.capture("sparse",round);c.inject(round);c.fault.set(3,round);verifyAll(c,live,false);c.capture("survivors",round);
        c.fault.set(4,round);if(round%4==3)clear(c,live);c.capture("released",round);c.fault.set(0,round);
    }
    clear(c,live);c.capture("empty",16);
}
void sparse(Context&c){
    for(unsigned round=0;round<4;++round){
        c.fault.set(1,round);std::vector<Item> live;
        for(unsigned size:{64u,256u,1024u,4096u})for(unsigned i=0;i<31;++i)live.push_back(c.make(size,round*1000+unsigned(live.size())+1));
        verifyAll(c,live,true);c.capture("full",round);c.fault.set(2,round);
        std::set<std::uint64_t> kept;std::vector<Item> survivors;
        for(auto&i:live){auto memory=c.model.live.at(i.native).entry.memory;if(kept.insert(memory).second)survivors.push_back(i);else c.drop(i);}live.clear();live=std::move(survivors);
        c.capture("one-per-block",round);c.inject(round);c.fault.set(3,round);verifyAll(c,live,false);
        std::vector<Item> replacements;for(unsigned i=0;i<32;++i)replacements.push_back(c.make(std::array{64u,256u,1024u,4096u}[i%4],round*1000+500+i));
        verifyAll(c,replacements,true);verifyAll(c,live,false);c.capture("holes-refilled",round);
        c.fault.set(4,round);clear(c,replacements);clear(c,live);c.capture("empty",round);
        auto singleton=c.make(4096,round+3);c.capture("reset-singleton",round);c.drop(singleton);c.capture("reset-empty",round);c.fault.set(0,round);
    }
}
void held(Context&c){
    for(unsigned round=0;round<8;++round){
        c.fault.set(1,round);std::vector<Item> live,read;
        for(unsigned i=0;i<24;++i)live.push_back(c.make(sizes[i],round*101+i+1));verifyAll(c,live,true);
        auto cmd=take(c.d.createCommandList());take(cmd->begin());
        for(auto&i:live){read.push_back(c.make(i.size,i.seed,gfx::Memory::Readback));take(cmd->copyBuffer(i.buffer,read.back().buffer,i.size));}take(cmd->end());
        for(auto&i:live){std::weak_ptr<gfx::Buffer> weak=i.buffer;i.buffer.reset();check(weak.expired(),"Ended recording retains allocation without wrapper");}
        c.capture("ended-retained",round);c.fault.set(2,round);c.inject(round);
        std::vector<Item> replacements;for(unsigned i=0;i<72;++i)replacements.push_back(c.make(sizes[(i+round)%sizes.size()],round*211+100+i));verifyAll(c,replacements,true);c.capture("replacements",round);
        c.fault.set(3,round);auto fence=take(c.d.queue().submit(*cmd));cmd.reset();c.capture("pending-retained",round,false);take(c.d.queue().wait(fence));
        for(unsigned i=0;i<read.size();++i){check(take(read[i].buffer->read(0,read[i].size))==payload(read[i].size,read[i].seed),"Retained slots survive distinct replacement writes");++verified;verifiedBytes+=read[i].size;c.model.remove(live[i].native);c.logicalSizes.erase(live[i].native);}
        live.clear();clear(c,read);verifyAll(c,replacements,false);c.capture("completed",round);
        c.fault.set(4,round);clear(c,replacements);c.capture("empty",round);c.fault.set(0,round);
    }
}
std::string validationLayer(bool enabled){auto m=GetModuleHandleW(L"VkLayer_khronos_validation.dll");if(!enabled){check(!m,"Unexpected VVL");return{};}wchar_t actual[32768]{},expected[32768]{};auto n=m?GetModuleFileNameW(m,actual,32768):0;auto e=GetEnvironmentVariableW(L"GFX_EXPECT_VULKAN_LAYER",expected,32768);check(n&&n<32768&&e&&e<32768&&std::filesystem::equivalent(actual,expected),"Pinned VVL path");return std::filesystem::path(actual).generic_string();}
}
int main(int argc,char**argv)try{
    using bench::quote;
    if(argc==2&&std::string_view(argv[1])=="--probe"){auto a=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));std::cout<<"{\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"adapters\":[";bool comma{};for(auto&x:a)if(!x.softwareAdapter){if(comma)std::cout<<',';comma=true;std::cout<<quote(x.name);}std::cout<<"]}\n";return 0;}
    if(argc!=5)throw std::runtime_error("Expected mixed/sparse/held exact-GPU validation-0/1 candidate-0/1");
    auto profile=std::string_view(argv[1]);if((profile!="mixed"&&profile!="sparse"&&profile!="held")||(std::string_view(argv[3])!="0"&&std::string_view(argv[3])!="1")||(std::string_view(argv[4])!="0"&&std::string_view(argv[4])!="1"))throw std::runtime_error("Invalid churn CLI");
    bool validation=std::string_view(argv[3])=="1",candidate=std::string_view(argv[4])=="1";
    auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));auto a=std::find_if(adapters.begin(),adapters.end(),[&](auto&x){return x.name==argv[2]&&!x.softwareAdapter;});
    if(a==adapters.end()||std::count_if(adapters.begin(),adapters.end(),[&](auto&x){return x.name==argv[2];})!=1)throw std::runtime_error("Unique exact hardware GPU required");auto identity=upload::identify(a->name);
    gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=a->id;desc.validation=validation;auto d=take(gfx::createDevice(desc));Context c(*d,candidate);auto layer=validationLayer(validation);
    c.capture("initial",0);if(profile=="mixed")mixed(c);else if(profile=="sparse")sparse(c);else held(c);
    take(d->queue().waitIdle());c.capture("final",99);check(c.model.live.empty()&&c.classes.size()>=(profile=="sparse"?3u:4u),"Empty final ownership and several actual native classes");
    if(candidate)check(c.model.reuses>0&&c.model.growths>0,"Candidate sharing actually exercised");if(validation)check(take(d->validationErrorCount())==0,"No validation errors");
    c.fault.set(6,99);d.reset();c.fault.set(0,99);check(!c.fault.status(),"No native guards/pending faults");auto final=c.snapshot();check(!final->memories&&!final->bytes&&!final->buffers&&!final->images,"Native teardown empty");
    std::cout<<"{\"schema_version\":1,\"contract\":\"vulkan_pool_churn_v1\",\"profile\":"<<quote(profile)<<",\"candidate\":"<<(candidate?"true":"false")<<",\"validation\":"<<(validation?"true":"false")<<",\"SDK_source_fingerprint\":"<<quote(gfx::buildFingerprint())<<",\"source_fingerprint\":"<<quote(GFX_TRACE_FINGERPRINT)<<",\"device_name\":"<<quote(a->name)<<",\"device_uuid\":"<<quote(identity.uuid)<<",\"driver_uuid\":"<<quote(identity.driverUuid)<<",\"loaded_validation_layer\":"<<quote(layer)<<",\"environment\":"<<bench::environment(a->name)<<",\"assertions\":"<<assertions<<",\"verified_buffers\":"<<verified<<",\"verified_bytes\":"<<verifiedBytes<<",\"model\":{\"creates\":"<<c.model.creates<<",\"reuses\":"<<c.model.reuses<<",\"growths\":"<<c.model.growths<<",\"dedicated\":"<<c.model.dedicated<<",\"checks\":"<<c.model.checks<<"},\"strides\":[";
    bool comma{};for(auto s:c.classes){if(comma)std::cout<<',';comma=true;std::cout<<s;}std::cout<<"],\"checkpoints\":[";
    for(unsigned i=0;i<c.rows.size();++i){auto&r=c.rows[i];if(i)std::cout<<',';std::cout<<"{\"label\":"<<quote(r.label)<<",\"round\":"<<r.round<<",\"buffers\":"<<r.buffers<<",\"allocations\":"<<r.allocations<<",\"reserved\":"<<r.reserved<<",\"required\":"<<r.required<<",\"payload\":"<<r.payload<<",\"reuses\":"<<r.reuses<<",\"growths\":"<<r.growths<<'}';}
    std::cout<<"],\"native_trace\":"<<c.fault.result()<<"}\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
