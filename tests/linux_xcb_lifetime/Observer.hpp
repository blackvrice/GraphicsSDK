// Included only by the dedicated test proxy, inside Loader.cpp's private namespace.
#include "Control.hpp"
struct LifetimeGate {
    bool armed{}, idleEntered{}, presentEntered{}, complete{};
    VkDevice device{}; VkQueue queue{}; VkEvent event{}; VkCommandPool pool{}; VkFence fence{};
} gate;
lifetimeprobe::Snapshot lifetime;
unsigned fault{}, viewCountdown{};
std::map<VkImage,VkSwapchainKHR> swapImages;
std::map<VkImageView,VkSwapchainKHR> swapViews;
void cleanupGate() {
    if (!gate.event) return;
    auto destroyPool=deviceNext<PFN_vkDestroyCommandPool>(gate.device,"vkDestroyCommandPool");
    auto destroyEvent=deviceNext<PFN_vkDestroyEvent>(gate.device,"vkDestroyEvent");
    std::lock_guard lock(mutex); // A concurrent host SetEvent call must finish first.
    destroyPool(gate.device,gate.pool,nullptr);destroyEvent(gate.device,gate.event,nullptr);
    gate={}; ++lifetime.cleaned;
}
int lifetimeControl(unsigned op) {
    if(op==lifetimeprobe::Cleanup){
        if(!gate.event)return 0;
        auto r=deviceNext<PFN_vkQueueWaitIdle>(gate.device,"vkQueueWaitIdle")(gate.queue);
        if(r!=VK_SUCCESS)return 0;
        cleanupGate();return 1;
    }
    if(op==lifetimeprobe::Blocked){
        if(!gate.device||!gate.fence)return 0;
        auto r=deviceNext<PFN_vkGetFenceStatus>(gate.device,"vkGetFenceStatus")(gate.device,gate.fence);
        std::lock_guard lock(mutex);if(r==VK_NOT_READY){++lifetime.blocked;return 1;}return 0;
    }
    if(op==lifetimeprobe::Release){
        if(!gate.device||!gate.event)return 0;
        auto set=deviceNext<PFN_vkSetEvent>(gate.device,"vkSetEvent");
        std::lock_guard lock(mutex);auto r=set(gate.device,gate.event);
        if(r==VK_SUCCESS){++lifetime.released;return 1;}return 0;
    }
    std::lock_guard lock(mutex);
    if(op==lifetimeprobe::Arm){if(gate.event||gate.armed)return 0;gate={};gate.armed=true;return 1;}
    if(op==lifetimeprobe::IdleEntered)return gate.idleEntered;
    if(op==lifetimeprobe::PresentEntered)return gate.presentEntered;
    if(op==lifetimeprobe::FailFence||op==lifetimeprobe::FailSubmit||op==lifetimeprobe::FailSecondView){fault=op;viewCountdown=2;return 1;}
    return 0;
}
bool consumeFault(unsigned op){std::lock_guard lock(mutex);if(fault!=op)return false;if(op==lifetimeprobe::FailSecondView&&--viewCountdown)return false;fault=0;++lifetime.faults;return true;}
VKAPI_ATTR VkResult VKAPI_CALL lifetimeSubmit(VkQueue q,uint32_t count,const VkSubmitInfo* infos,VkFence fence){
    if(consumeFault(lifetimeprobe::FailSubmit))return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkDevice d;bool armed;{std::lock_guard lock(mutex);d=queues.at(q);armed=gate.armed;}
    if(!armed){auto r=deviceNext<PFN_vkQueueSubmit>(d,"vkQueueSubmit")(q,count,infos,fence);if(r==VK_SUCCESS){std::lock_guard lock(mutex);++lifetime.submits;}return r;}
    if(!count)return VK_ERROR_INITIALIZATION_FAILED;
    gate.device=d;gate.queue=q;gate.fence=fence;
    VkEventCreateInfo event{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
    auto r=deviceNext<PFN_vkCreateEvent>(d,"vkCreateEvent")(d,&event,nullptr,&gate.event);if(r!=VK_SUCCESS)return r;
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=totals.family;
    r=deviceNext<PFN_vkCreateCommandPool>(d,"vkCreateCommandPool")(d,&pool,nullptr,&gate.pool);if(r!=VK_SUCCESS)return r;
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=gate.pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
    VkCommandBuffer command{};r=deviceNext<PFN_vkAllocateCommandBuffers>(d,"vkAllocateCommandBuffers")(d,&allocate,&command);if(r!=VK_SUCCESS)return r;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    r=deviceNext<PFN_vkBeginCommandBuffer>(d,"vkBeginCommandBuffer")(command,&begin);if(r!=VK_SUCCESS)return r;
    deviceNext<PFN_vkCmdWaitEvents>(d,"vkCmdWaitEvents")(command,1,&gate.event,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,nullptr,0,nullptr,0,nullptr);
    r=deviceNext<PFN_vkEndCommandBuffer>(d,"vkEndCommandBuffer")(command);if(r!=VK_SUCCESS)return r;
    std::vector<VkSubmitInfo> copy(infos,infos+count);std::vector<VkCommandBuffer> commands{command};
    if(infos[0].commandBufferCount)commands.insert(commands.end(),infos[0].pCommandBuffers,infos[0].pCommandBuffers+infos[0].commandBufferCount);
    copy[0].commandBufferCount=commands.size();copy[0].pCommandBuffers=commands.data();
    r=deviceNext<PFN_vkQueueSubmit>(d,"vkQueueSubmit")(q,count,copy.data(),fence);
    {std::lock_guard lock(mutex);gate.armed=false;if(r==VK_SUCCESS){++lifetime.submits;++lifetime.gates;}}
    return r;
}
VKAPI_ATTR VkResult VKAPI_CALL lifetimeIdle(VkDevice d){
    {std::lock_guard lock(mutex);if(gate.event&&gate.device==d){gate.idleEntered=true;++lifetime.idleBoundaries;}}
    auto r=deviceNext<PFN_vkDeviceWaitIdle>(d,"vkDeviceWaitIdle")(d);
    if(r==VK_SUCCESS){std::lock_guard lock(mutex);if(gate.device==d)gate.complete=true;}return r;
}
VKAPI_ATTR VkResult VKAPI_CALL lifetimePresent(VkQueue q,const VkPresentInfoKHR* info){
    {std::lock_guard lock(mutex);if(gate.event&&gate.queue==q){gate.presentEntered=true;++lifetime.presentBoundaries;}}
    return present(q,info);
}
VKAPI_ATTR VkResult VKAPI_CALL lifetimeFence(VkDevice d,const VkFenceCreateInfo* info,const VkAllocationCallbacks* a,VkFence* out){if(consumeFault(lifetimeprobe::FailFence))return VK_ERROR_OUT_OF_HOST_MEMORY;auto r=deviceNext<PFN_vkCreateFence>(d,"vkCreateFence")(d,info,a,out);if(r==VK_SUCCESS){std::lock_guard lock(mutex);++lifetime.fences;}return r;}
VKAPI_ATTR void VKAPI_CALL lifetimeDestroyFence(VkDevice d,VkFence f,const VkAllocationCallbacks* a){
    if(gate.event&&gate.fence==f){auto r=deviceNext<PFN_vkGetFenceStatus>(d,"vkGetFenceStatus")(d,f);std::lock_guard lock(mutex);if(r==VK_SUCCESS)gate.complete=true;else ++lifetime.orderErrors;}
    deviceNext<PFN_vkDestroyFence>(d,"vkDestroyFence")(d,f,a);std::lock_guard lock(mutex);++lifetime.destroyedFences;
}
VKAPI_ATTR VkResult VKAPI_CALL lifetimeView(VkDevice d,const VkImageViewCreateInfo* info,const VkAllocationCallbacks* a,VkImageView* out){if(consumeFault(lifetimeprobe::FailSecondView))return VK_ERROR_OUT_OF_HOST_MEMORY;auto r=deviceNext<PFN_vkCreateImageView>(d,"vkCreateImageView")(d,info,a,out);if(r==VK_SUCCESS){std::lock_guard lock(mutex);++lifetime.views;if(auto it=swapImages.find(info->image);it!=swapImages.end())swapViews[*out]=it->second;}return r;}
VKAPI_ATTR void VKAPI_CALL lifetimeDestroyView(VkDevice d,VkImageView v,const VkAllocationCallbacks* a){deviceNext<PFN_vkDestroyImageView>(d,"vkDestroyImageView")(d,v,a);std::lock_guard lock(mutex);swapViews.erase(v);++lifetime.destroyedViews;}
VKAPI_ATTR VkResult VKAPI_CALL lifetimeSemaphore(VkDevice d,const VkSemaphoreCreateInfo* info,const VkAllocationCallbacks* a,VkSemaphore* out){auto r=deviceNext<PFN_vkCreateSemaphore>(d,"vkCreateSemaphore")(d,info,a,out);if(r==VK_SUCCESS){std::lock_guard lock(mutex);++lifetime.semaphores;}return r;}
VKAPI_ATTR void VKAPI_CALL lifetimeDestroySemaphore(VkDevice d,VkSemaphore s,const VkAllocationCallbacks* a){
    {std::lock_guard lock(mutex);if(std::any_of(presentFences.begin(),presentFences.end(),[&](auto& f){return f.second==d;}))++lifetime.orderErrors;}
    deviceNext<PFN_vkDestroySemaphore>(d,"vkDestroySemaphore")(d,s,a);std::lock_guard lock(mutex);++lifetime.destroyedSemaphores;
}
VKAPI_ATTR VkResult VKAPI_CALL lifetimeImages(VkDevice d,VkSwapchainKHR s,uint32_t* count,VkImage* images){auto r=chainImages(d,s,count,images);if(r==VK_SUCCESS&&images){std::lock_guard lock(mutex);for(unsigned n=0;n<*count;++n)swapImages[images[n]]=s;}return r;}
VKAPI_ATTR void VKAPI_CALL lifetimeDestroyChain(VkDevice d,VkSwapchainKHR s,const VkAllocationCallbacks* a){
    {std::lock_guard lock(mutex);if(std::any_of(swapViews.begin(),swapViews.end(),[&](auto& v){return v.second==s;})||(gate.event&&gate.device==d&&!gate.complete))++lifetime.orderErrors;std::erase_if(swapImages,[&](auto& v){return v.second==s;});}
    destroyChain(d,s,a);
}
VKAPI_ATTR void VKAPI_CALL lifetimeDestroyDevice(VkDevice d,const VkAllocationCallbacks* a){
    if(gate.event&&gate.device==d){if(!gate.complete)++lifetime.orderErrors;cleanupGate();}
    destroyDevice(d,a);
}
PFN_vkVoidFunction lifetimeHook(const char* name){
#define L(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    L(QueueSubmit,lifetimeSubmit) L(DeviceWaitIdle,lifetimeIdle) L(QueuePresentKHR,lifetimePresent)
    L(CreateFence,lifetimeFence) L(DestroyFence,lifetimeDestroyFence) L(CreateImageView,lifetimeView) L(DestroyImageView,lifetimeDestroyView)
    L(CreateSemaphore,lifetimeSemaphore) L(DestroySemaphore,lifetimeDestroySemaphore) L(GetSwapchainImagesKHR,lifetimeImages) L(DestroySwapchainKHR,lifetimeDestroyChain) L(DestroyDevice,lifetimeDestroyDevice)
#undef L
    return nullptr;
}
