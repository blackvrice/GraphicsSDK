#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks{},devices{},images{};std::uint64_t bytes{};
void check(bool b){++checks;if(!b)throw std::runtime_error("Pool GPU contract");}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
gfx::DeviceDesc desc(const gfx::AdapterInfo&a,bool validation){gfx::DeviceDesc d;d.backend=gfx::Backend::Vulkan;d.adapter=a.id;d.softwareAdapter=a.softwareAdapter;d.validation=validation;return d;}
void empty(gfx::Device&d){auto s=take(d.resourceStatistics());check(!s.trackedObjects&&!s.uniqueResources&&!s.resourceAllocationBytes&&!s.pendingSubmissions&&!s.pendingObjectReferences);}
void run(const gfx::AdapterInfo&a,bool validation){
    auto d=take(gfx::createDevice(desc(a,validation)));++devices;
    // Fresh sparse demand grows 1,2,4,8,16 slots without a 64KiB first buffer.
    for(auto count:{1u,2u,3u,4u,7u,8u,15u,16u,31u,32u}){
        std::vector<gfx::BufferPtr> sparse;
        for(unsigned i=0;i<count;++i)sparse.push_back(take(d->createBuffer({4096})));
        unsigned slots{},largest{};while(slots<count){largest=largest?std::min(largest*2,16u):1u;slots+=largest;}
        auto stats=take(d->resourceStatistics());check(stats.uniqueResources==count&&stats.resourceAllocationBytes==slots*4096);
        sparse.clear();empty(*d);
    }
    std::vector<gfx::BufferPtr> many;
    for(unsigned i=0;i<1000;++i)many.push_back(take(d->createBuffer({4096})));
    auto s=take(d->resourceStatistics());check(s.uniqueResources==1000);check(s.resourceAllocationBytes==1007*4096);
    for(unsigned i=0;i<1000;i+=2)many[i].reset();
    for(unsigned i=0;i<1000;i+=2)many[i]=take(d->createBuffer({4096}));
    s=take(d->resourceStatistics());check(s.uniqueResources==1000&&s.resourceAllocationBytes>=1000*4096&&s.resourceAllocationBytes<=1007*4096);
    // An occupied large block stays until its final lease; no migration is implied.
    auto survivor=many.back();many.clear();s=take(d->resourceStatistics());check(s.uniqueResources==1&&s.resourceAllocationBytes==65536);
    survivor.reset();empty(*d);
    auto singleton=take(d->createBuffer({4096}));check(take(d->resourceStatistics()).resourceAllocationBytes==4096);singleton.reset();empty(*d);
    auto old=take(d->createBuffer({4096})),up=take(d->createBuffer({4096,gfx::Memory::Upload})),read=take(d->createBuffer({4096,gfx::Memory::Readback}));
    std::vector<std::byte> expected(4096,std::byte{0x79});take(up->write(0,expected));
    auto initialize=take(d->createCommandList());take(initialize->begin());take(initialize->transition(old,gfx::ResourceState::CopyDestination));take(initialize->copyBuffer(up,old,4096));take(initialize->transition(old,gfx::ResourceState::CopySource));take(initialize->end());take(d->queue().wait(take(d->queue().submit(*initialize))));initialize.reset();
    auto held=take(d->createCommandList());take(held->begin());take(held->copyBuffer(old,read,4096));take(held->end());std::weak_ptr<gfx::Buffer>w=old;old.reset();check(w.expired());
    // The ended recording retains the old native buffer and its slot, before submit.
    std::vector<gfx::BufferPtr> replacements;
    for(unsigned i=0;i<32;++i)replacements.push_back(take(d->createBuffer({4096})));
    take(up->write(0,std::vector<std::byte>(4096,std::byte{0x26})));
    auto replace=take(d->createCommandList());take(replace->begin());
    for(auto& p:replacements){take(replace->transition(p,gfx::ResourceState::CopyDestination));take(replace->copyBuffer(up,p,4096));}
    take(replace->end());take(d->queue().wait(take(d->queue().submit(*replace))));replace.reset();
    take(d->queue().wait(take(d->queue().submit(*held))));check(take(read->read(0,4096))==expected);bytes+=4096;
    held.reset();replacements.clear();up.reset();read.reset();empty(*d);
    // Mixed size/alignment classes plus the >4KiB dedicated fallback.
    for(auto size:{1u,63u,64u,65u,257u,4095u,4096u,4097u,65537u}){
        auto u=take(d->createBuffer({size,gfx::Memory::Upload})),g=take(d->createBuffer({size})),r=take(d->createBuffer({size,gfx::Memory::Readback}));
        std::vector<std::byte> payload(size);for(unsigned i=0;i<size;++i)payload[i]=std::byte((i*37u+size)&255);take(u->write(0,payload));
        auto c=take(d->createCommandList());take(c->begin());take(c->transition(g,gfx::ResourceState::CopyDestination));take(c->copyBuffer(u,g,size));take(c->transition(g,gfx::ResourceState::CopySource));take(c->copyBuffer(g,r,size));take(c->end());auto fence=take(d->queue().submit(*c));std::weak_ptr<gfx::Buffer> weak=g;g.reset();c.reset();check(weak.expired());take(d->queue().wait(fence));check(take(r->read(0,size))==payload);bytes+=size;
    }
    empty(*d);
    auto late=take(d->createBuffer({64}));d.reset();check(late->description().size==64);late.reset();
    // Independent generation must not reuse retired native blocks.
    d=take(gfx::createDevice(desc(a,validation)));auto fresh=take(d->createBuffer({64}));fresh.reset();empty(*d);
    if(validation)check(take(d->validationErrorCount())==0);
    std::cout<<"PASS device="<<a.name<<" validation="<<validation<<'\n';
}
}
int main(int argc,char**argv)try{
    const bool validation=argc==2&&std::string_view(argv[1])=="--validation";if(argc!=1&&!validation)throw std::runtime_error("Invalid pool test option");
    for(auto&a:take(gfx::enumerateAdapters(gfx::Backend::Vulkan)))if(!a.softwareAdapter)run(a,validation);check(devices>=2);
    std::cout<<"PASS pool_gpu_assertions="<<checks<<" devices="<<devices<<" verified_bytes="<<bytes<<" images="<<images<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
