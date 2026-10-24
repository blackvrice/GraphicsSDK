// Compile the actual implementation in this TU to inject PFNs without exposing
// test switches in the public API or loading a Vulkan driver.
#include GFX_VULKAN_IMPLEMENTATION
#include "../VulkanBindingSupport.hpp"
#include <iostream>
#include <map>
#include <string>
#include <type_traits>

namespace gfx::creationtest {
struct Object {std::string kind;std::uint64_t parent{},size{4096};};
struct Ledger {
    std::map<std::uint64_t,Object> live;
    std::string fail;unsigned ordinal{1},injected{},invalid{},calls{},created{},destroyed{},checks{};
    std::uint64_t next{0x10000};
} g;
template<class H>std::uint64_t id(H h){return reinterpret_cast<std::uint64_t>(h);}
void check(bool b){++g.checks;if(!b)throw std::runtime_error("Actual creation implementation contract");}
bool fails(const char*name){++g.calls;if(g.fail==name&&--g.ordinal==0){++g.injected;return true;}return false;}
template<class H,class Info>VkResult create(const char*name,const Info*info,H*out,bool nullOnError=false){
    if(fails(name)){*out=nullOnError?H{}:reinterpret_cast<H>(std::uintptr_t(0xf11));return VK_ERROR_OUT_OF_HOST_MEMORY;}
    *out=reinterpret_cast<H>(++g.next);Object o{name};
    if constexpr(std::is_same_v<Info,VkBufferCreateInfo>)o.size=info->size;
    if constexpr(std::is_same_v<Info,VkCommandBufferAllocateInfo>)o.parent=id(info->commandPool);
    if constexpr(std::is_same_v<Info,VkDescriptorSetAllocateInfo>)o.parent=id(info->descriptorPool);
    g.live.emplace(id(*out),o);++g.created;return VK_SUCCESS;
}
template<class H>void destroy(H h){
    if(!h)return;auto it=g.live.find(id(h));if(it==g.live.end()){++g.invalid;return;}
    for(auto child=g.live.begin();child!=g.live.end();)if(child->second.parent==id(h)){++g.destroyed;child=g.live.erase(child);}else++child;
    g.live.erase(it);++g.destroyed;
}
#include "Stubs.hpp"
struct Context {
    VulkanDevice d;
    Context(){
        auto native=std::make_shared<NativeDevice>();native->handle=reinterpret_cast<VkDevice>(1);native->instance=std::make_shared<Instance>();
#define SET(name) native->name=f##name;
        DEVICE_FUNCTIONS(SET)
#undef SET
#define SET(name) native->instance->name=f##name;
        INSTANCE_FUNCTIONS(SET)
#undef SET
        native->CreateSwapchainKHR=fCreateSwapchainKHR;native->DestroySwapchainKHR=fDestroySwapchainKHR;native->GetSwapchainImagesKHR=fGetSwapchainImagesKHR;
        auto state=std::make_shared<State>();state->native=native;state->memory.memoryTypeCount=1;state->memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        state->maxColorAttachments=8;state->maxFramebufferWidth=state->maxFramebufferHeight=65536;state->timestampBits=64;
        std::memset(&state->limits,0xff,sizeof(state->limits));state->limits.timestampPeriod=1;state->limits.minUniformBufferOffsetAlignment=256;
        d.owner=state;d.commands.owner=state;
    }
};
template<class T>T take(Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
template<class T>void result(Result<T> r,bool failure){check(bool(r)!=failure);if(!r){check(r.error().code==ErrorCode::OutOfMemory);check(r.error().nativeCode==VK_ERROR_OUT_OF_HOST_MEMORY);}}
struct Case {const char*name;const char*api;unsigned ordinal{1};};
constexpr Case cases[]={
    {"buffer","CreateBuffer"},{"buffer-memory","AllocateMemory"},{"upload-memory","AllocateMemory"},{"readback-memory","AllocateMemory"},
    {"image","CreateImage"},{"image-memory","AllocateMemory"},{"image-view","CreateImageView"},
    {"command-pool","CreateCommandPool"},{"command-array","AllocateCommandBuffers"},{"query-pool","CreateQueryPool"},{"submit-fence","CreateFence"},
    {"sampler","CreateSampler"},{"sampler-second","CreateSampler",2},{"set-layout","CreateDescriptorSetLayout"},
    {"shader","CreateShaderModule"},{"shader-second","CreateShaderModule",2},{"pipeline-layout","CreatePipelineLayout"},
    {"pipeline-pass","CreateRenderPass"},{"pipeline-array","CreateGraphicsPipelines"},
    {"pass","CreateRenderPass"},{"framebuffer","CreateFramebuffer"},{"descriptor-pool","CreateDescriptorPool"},{"descriptor-array","AllocateDescriptorSets"},
    {"swapchain","CreateSwapchainKHR"},{"swap-view","CreateImageView"},{"swap-semaphore","CreateSemaphore"},{"swap-semaphore-second","CreateSemaphore",2}
};
void run(const Case&test,bool failure){
    Context c;const std::string name=test.name;g.fail=failure?test.api:"";g.ordinal=test.ordinal;
    if(name=="buffer"||name=="buffer-memory"||name=="upload-memory"||name=="readback-memory"){
        result(c.d.createBuffer({4096,name=="upload-memory"?Memory::Upload:name=="readback-memory"?Memory::Readback:Memory::Device}),failure);
    }else if(name=="image"||name=="image-memory"||name=="image-view"){
        result(c.d.createTexture({4,4,Format::RGBA8UNorm,true}),failure);
    }else if(name=="command-pool"||name=="command-array"||name=="query-pool"||name=="submit-fence"){
        auto cmd=take(c.d.createCommandList());if(name=="submit-fence"){take(cmd->begin());take(cmd->end());result(c.d.queue().submit(*cmd),failure);}
        else result(cmd->begin(),failure);
    }else if(name.starts_with("swap")){
        VulkanSwapchain swap;swap.owner=c.d.owner;swap.surface=std::make_shared<NativeSurface>();swap.surface->native=c.d.owner->native;
        VkSwapchainCreateInfoKHR config{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};config.imageExtent={4,4};result(swap.rebuild(config),failure);
    }else if(name=="pass"||name=="framebuffer"||name=="descriptor-pool"||name=="descriptor-array"){
        auto target=take(c.d.createTexture({4,4,Format::RGBA8UNorm,true}));auto cmd=take(c.d.createCommandList());take(cmd->begin());take(cmd->transition(target,ResourceState::RenderTarget));
        if(name=="pass"||name=="framebuffer")result(cmd->beginRenderPass(target,{}),failure);
        else{
            auto p=take(c.d.createGraphicsPipeline(bindingTest::pipeline()));auto sample=take(c.d.createTexture({4,4,Format::RGBA8UNorm}));
            auto a=std::dynamic_pointer_cast<VulkanTexture>(sample)->allocation;a->state=ResourceState::ShaderRead;a->contentsInitialized=a->imageInitialized=true;
            take(cmd->beginRenderPass(target,{}));take(cmd->setPipeline(p));result(cmd->bindTexture(sample),failure);
        }
    }else result(c.d.createGraphicsPipeline(bindingTest::pipeline(name=="sampler-second")),failure);
}
}
int main()try{
    using namespace gfx::creationtest;
    unsigned totalInvalid{},totalChecks{},totalCreated{};std::cout<<"{\"SDK_source_fingerprint\":\""<<gfx::buildFingerprint()<<"\",\"cases\":[";bool comma{};
    for(auto&test:cases){g={};run(test,true);check(g.injected==1);check(g.live.empty());check(g.created==g.destroyed);auto invalid=g.invalid;totalInvalid+=invalid;
        g.fail.clear();run(test,false);check(g.live.empty());check(g.created==g.destroyed);check(g.invalid==invalid);
        if(comma)std::cout<<',';comma=true;std::cout<<"{\"case\":\""<<test.name<<"\",\"api\":\""<<test.api<<"\",\"ordinal\":"<<test.ordinal<<",\"invalid_cleanup\":"<<invalid<<",\"created\":"<<g.created<<",\"destroyed\":"<<g.destroyed<<",\"live\":"<<g.live.size()<<",\"assertions\":"<<g.checks<<'}';totalChecks+=g.checks;totalCreated+=g.created;
    }
    std::cout<<"],\"invalid_cleanup\":"<<totalInvalid<<",\"assertions\":"<<totalChecks<<",\"created\":"<<totalCreated<<",\"GPU_calls\":0}\n";return totalInvalid?2:0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
