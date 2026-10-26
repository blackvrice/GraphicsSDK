#include <graphics/Graphics.hpp>
#include "fixtures/SpirvFixtures.hpp"
#include "vulkan_recovery/Control.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
using S=gfx::ResourceState;using E=gfx::ErrorCode;
unsigned assertions{},images{},cycles{};std::uint64_t verifiedBytes{},verifiedBufferBytes{};
void check(bool value,const char* text){++assertions;if(!value)throw std::runtime_error(text);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message+" native="+std::to_string(r.error().nativeCode));return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
template<class T>void reject(gfx::Result<T> r,E code){check(!r&&r.error().code==code,"Wrong recovery rejection");}
template<class T>void lost(gfx::Result<T> r){check(!r&&r.error().code==E::DeviceLost&&r.error().nativeCode==-4,"Lost reason must remain VK_ERROR_DEVICE_LOST");}
struct Control {
#ifdef _WIN32
    HMODULE module{};
#else
    void* module{};
#endif
    recoveryprobe::Arm armFunction{};recoveryprobe::Query query{};recoveryprobe::Reverse reverse{};
    explicit Control(const std::filesystem::path& path){
#ifdef _WIN32
        module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        auto symbol=[&](const char* name){return module?GetProcAddress(module,name):nullptr;};
#else
        module=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);auto symbol=[&](const char* name){return module?dlsym(module,name):nullptr;};
#endif
        check(module!=nullptr,"Missing recovery control module");armFunction=reinterpret_cast<recoveryprobe::Arm>(symbol("gfxRecoveryArm"));
        query=reinterpret_cast<recoveryprobe::Query>(symbol("gfxRecoverySnapshot"));reverse=reinterpret_cast<recoveryprobe::Reverse>(symbol("gfxRecoveryReverse"));check(armFunction&&query&&reverse,"Missing recovery controls");
    }
    void arm(unsigned mode){check(armFunction(mode)!=0,"One-shot recovery control not armed");}
    recoveryprobe::Snapshot snapshot(){recoveryprobe::Snapshot s;check(query(&s,sizeof(s))!=0,"Recovery snapshot");return s;}
    ~Control(){
#ifdef _WIN32
        if(module)FreeLibrary(module);
#else
        if(module)dlclose(module);
#endif
    }
};
gfx::DeviceDesc description(const gfx::AdapterInfo& a,bool validation){gfx::DeviceDesc d;d.backend=gfx::Backend::Vulkan;d.adapter=a.id;d.softwareAdapter=a.softwareAdapter;d.validation=validation;return d;}
gfx::GraphicsPipelineDesc pipeline(){
    auto binary=[](std::span<const std::uint32_t> words,gfx::ShaderStage stage){auto b=std::as_bytes(words);return gfx::ShaderBinary{stage,{b.begin(),b.end()},gfx::ShaderFormat::SPIRV};};
    gfx::GraphicsPipelineDesc p;p.vertex=binary(fixtures::fullscreen,gfx::ShaderStage::Vertex);p.fragment=binary(fixtures::red,gfx::ShaderStage::Fragment);return p;
}
struct Assets {gfx::BufferPtr upload,readback,imageRead,padding;gfx::TexturePtr target;gfx::PipelinePtr pipeline;std::vector<std::byte> bytes;};
Assets assets(gfx::Device& d,const gfx::GraphicsPipelineDesc& cpu,unsigned seed){
    Assets a;a.bytes.resize(4097);for(std::size_t i=0;i<a.bytes.size();++i)a.bytes[i]=std::byte((i*13+seed)%256);
    a.upload=take(d.createBuffer({a.bytes.size(),gfx::Memory::Upload}));take(a.upload->write(0,a.bytes));a.readback=take(d.createBuffer({a.bytes.size(),gfx::Memory::Readback}));
    a.target=take(d.createTexture({33,17,gfx::Format::RGBA8UNorm,true}));a.pipeline=take(d.createGraphicsPipeline(cpu));const auto f=a.target->footprint();
    a.imageRead=take(d.createBuffer({f.size,gfx::Memory::Readback}));a.padding=take(d.createBuffer({f.size,gfx::Memory::Upload}));take(a.padding->write(0,std::vector<std::byte>(static_cast<std::size_t>(f.size),std::byte{0xa7})));return a;
}
std::unique_ptr<gfx::CommandList> record(gfx::Device& d,const Assets& a){
    auto c=take(d.createCommandList());take(c->begin());take(c->copyBuffer(a.upload,a.readback,a.bytes.size()));take(c->copyBuffer(a.padding,a.imageRead,a.target->footprint().size));
    take(c->transition(a.target,S::RenderTarget));take(c->beginRenderPass(a.target,{0,0,1,1}));take(c->setPipeline(a.pipeline));take(c->draw(3));take(c->endRenderPass());
    take(c->transition(a.target,S::CopySource));take(c->copyTextureToBuffer(a.target,a.imageRead));take(c->end());return c;
}
void image(const gfx::BufferPtr& read,gfx::TextureFootprint f){
    const auto bytes=take(read->read(0,f.size));bool pixels=true,padding=true;
    for(unsigned y=0;y<f.height;++y){
        for(unsigned x=0;x<f.rowBytes/4;++x){const std::array<unsigned char,4> red{255,0,0,255};for(unsigned k=0;k<4;++k)pixels&=std::to_integer<unsigned char>(bytes[y*f.rowPitch+x*4+k])==red[k];}
        for(std::uint64_t x=f.rowBytes;x<f.rowPitch;++x)padding&=bytes[y*f.rowPitch+x]==std::byte{0xa7};
    }check(pixels&&padding,"Full rebuilt image and padding reference");++images;verifiedBytes+=std::uint64_t(f.rowBytes)*f.height;
}
gfx::Fence frame(gfx::Device& d,const Assets& a,std::unique_ptr<gfx::CommandList>& c){
    c=record(d,a);auto fence=take(d.queue().submit(*c));take(d.queue().wait(fence));check(take(a.readback->read(0,a.bytes.size()))==a.bytes,"Full rebuilt buffer reference");
    verifiedBufferBytes+=a.bytes.size();image(a.imageRead,a.target->footprint());return fence;
}
void blocked(gfx::Device& d,const Assets& old,gfx::CommandList& c,gfx::Fence fence,const gfx::GraphicsPipelineDesc& cpu){
    lost(d.checkStatus());lost(d.resourceStatistics());lost(d.queryMemoryBudget(gfx::MemorySegment::Local));lost(d.removeForTesting());
    lost(d.createBuffer({16}));lost(d.createTexture({4,4}));lost(d.createGraphicsPipeline(cpu));lost(d.createCommandList());lost(d.createSwapChain({},{}));
    lost(old.upload->write(0,{}));lost(old.readback->read(0,1));lost(c.begin());lost(c.end());lost(c.debugMarker("old"));lost(c.elapsedGpuMilliseconds());
    lost(d.queue().submit(c));lost(d.queue().wait(fence));lost(d.queue().waitIdle());lost(d.queue().completedValue());
    check(old.upload->description().size==4097&&old.target->description().width==33&&old.target->footprint().height==17,"Old CPU descriptions survive retirement");
}
void exercise(Control& control,gfx::DeviceDesc desc,unsigned mode,bool automatic){
    const auto before=control.snapshot();if(automatic)desc.adapter.reset();auto d=take(gfx::createDevice(desc));const auto selected=d->caps().adapterId;
    check(d->caps().deviceRecovery&&!d->caps().deviceLossInjection,"Recovery selected; injection remains unavailable");reject(d->recover(),E::InvalidState);reject(d->removeForTesting(),E::Unsupported);
    const auto cpu=pipeline();auto old=assets(*d,cpu,cycles+3);std::unique_ptr<gfx::CommandList> oldCommand;auto oldFence=frame(*d,old,oldCommand);
    auto pending=record(*d,old);control.arm(mode);auto submit=d->queue().submit(*pending);
    if(mode==recoveryprobe::PollLoss){oldFence=take(std::move(submit));lost(d->checkStatus());}else lost(std::move(submit));
    const auto stopped=control.snapshot();blocked(*d,old,*oldCommand,oldFence,cpu);check(control.snapshot().workCalls==stopped.workCalls,"Lost operations issue no observed native work");
    bool threadRejected{};std::thread thread([&]{auto r=d->recover();threadRejected=!r&&r.error().code==E::InvalidState;});thread.join();check(threadRejected,"Recovery thread affinity");
    if(automatic)control.reverse(1);
    control.arm(recoveryprobe::CreateOOM);reject(d->recover(),E::OutOfMemory);lost(d->checkStatus());
    const auto firstRetry=control.snapshot();check(firstRetry.destroyed==before.destroyed+1,"Old native device retired before a failed recreation");
    control.arm(recoveryprobe::CreateLoss);lost(d->recover());lost(d->checkStatus());auto fresh=take(d->recover());if(automatic)control.reverse(0);
    check(fresh->caps().adapterId==selected&&fresh->caps().softwareAdapter==desc.softwareAdapter,"Recovery pins exact selected UUID and policy");reject(d->recover(),E::InvalidState);reject(fresh->recover(),E::InvalidState);
    const auto retired=control.snapshot();blocked(*d,old,*oldCommand,oldFence,cpu);check(control.snapshot().workCalls==retired.workCalls,"Retired operations issue no observed native work");
    auto a=assets(*fresh,cpu,cycles+101);auto current=take(fresh->createCommandList());take(current->begin());
    reject(current->copyBuffer(old.upload,a.readback,16),E::WrongDevice);reject(current->transition(old.target,S::RenderTarget),E::WrongDevice);
    take(current->transition(a.target,S::RenderTarget));take(current->beginRenderPass(a.target));reject(current->setPipeline(old.pipeline),E::WrongDevice);take(current->endRenderPass());take(current->end());
    take(fresh->queue().wait(take(fresh->queue().submit(*current))));current.reset();reject(fresh->queue().wait(oldFence,std::chrono::milliseconds(0)),E::WrongDevice);
    std::unique_ptr<gfx::CommandList> rebuilt;auto newFence=frame(*fresh,a,rebuilt);check(newFence.device!=oldFence.device,"Fresh fence generation differs");
    pending.reset();oldCommand.reset();old={};d.reset();take(fresh->checkStatus());rebuilt.reset();a={};take(fresh->queue().waitIdle());
    const auto stats=take(fresh->resourceStatistics());check(!stats.trackedObjects&&!stats.uniqueResources&&!stats.resourceAllocationBytes&&!stats.pendingSubmissions&&!stats.pendingObjectReferences,"All rebuilt resources released");
    check(take(fresh->validationErrorCount())==0,"Fresh validation errors");fresh.reset();const auto after=control.snapshot();
    check(!after.armed&&!after.liveDevices&&!after.liveInstances&&!after.errors&&!after.prematureCreations,"Recovery native closure and ordering");
    check(after.created==before.created+2&&after.destroyed==before.destroyed+2&&after.failedCreates==before.failedCreates+2,"Real creations and retry failures balance");
    check(after.injected[mode]==before.injected[mode]+1&&after.drains==before.drains+1,"Exactly one safely drained synthetic loss");++cycles;
    std::cout<<"{\"event\":\"cycle\",\"mode\":"<<mode<<",\"adapter_id\":"<<selected.value<<",\"automatic\":"<<std::boolalpha<<automatic<<",\"old_generation\":"<<oldFence.device<<",\"fresh_generation\":"<<newFence.device<<",\"passed\":true}\n";
}
}
int main(int argc,char** argv)try{
    bool validation{},contracts{};std::filesystem::path library;
    for(int i=1;i<argc;++i){std::string_view arg=argv[i];if(arg=="--validation")validation=true;else if(arg=="--contracts")contracts=true;else if(arg=="--observe"&&i+1<argc&&library.empty())library=std::filesystem::absolute(argv[++i]);else throw std::runtime_error("Usage: [--validation] --contracts | --observe library");}
    check(contracts!=!library.empty(),"Select exactly one recovery test mode");const auto adapters=take(gfx::enumerateAdapters(gfx::Backend::Vulkan));check(!adapters.empty(),"Actual Vulkan adapter required");
    if(contracts){for(const auto& a:adapters){auto d=take(gfx::createDevice(description(a,validation)));check(d->caps().deviceRecovery&&!d->caps().deviceLossInjection,"Honest recovery caps");reject(d->recover(),E::InvalidState);take(d->checkStatus());auto c=take(d->createCommandList());d.reset();lost(c->begin());}std::cout<<"PASS recovery contracts assertions="<<assertions<<'\n';return 0;}
    Control control(library);
    for(const auto& a:adapters){std::cout<<"{\"event\":\"adapter\",\"adapter_id\":"<<a.id.value<<",\"name\":"<<std::quoted(a.name)<<",\"software\":"<<std::boolalpha<<a.softwareAdapter<<"}\n";for(auto mode:{recoveryprobe::SubmitLoss,recoveryprobe::PollLoss,recoveryprobe::FenceLoss})exercise(control,description(a,validation),mode,false);}
    exercise(control,description(adapters.front(),validation),recoveryprobe::SubmitLoss,true);auto s=control.snapshot();
    std::cout<<"{\"event\":\"result\",\"source\":"<<std::quoted(gfx::buildFingerprint())<<",\"validation\":"<<std::boolalpha<<validation<<",\"synthetic_loss\":true,\"actual_TDR\":false,\"adapters\":"<<adapters.size()<<",\"cycles\":"<<cycles<<",\"images\":"<<images<<",\"verified_bytes\":"<<verifiedBytes<<",\"verified_buffer_bytes\":"<<verifiedBufferBytes<<",\"created\":"<<s.created<<",\"destroyed\":"<<s.destroyed<<",\"failed_creates\":"<<s.failedCreates<<",\"drains\":"<<s.drains<<",\"reversed_enumerations\":"<<s.reversedEnumerations<<",\"errors\":"<<s.errors<<",\"premature_creations\":"<<s.prematureCreations<<",\"live_instances\":"<<s.liveInstances<<",\"live_devices\":"<<s.liveDevices<<",\"assertions\":"<<assertions<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
