// Dedicated test proxy only; normal native calls retain the lifetime observer.
resizeprobe::Snapshot resizeStats;
unsigned resizeOperation{};
std::map<VkSwapchainKHR,bool> resizeChains;
int resizeControl(unsigned op){
    if(op<1||op>7)return 0;
    std::lock_guard lock(mutex);
    if(op==resizeprobe::FailSecondView){fault=lifetimeprobe::FailSecondView;viewCountdown=2;return 1;}
    resizeOperation=op;return 1;
}
bool consumeResize(unsigned op){std::lock_guard lock(mutex);if(resizeOperation!=op)return false;resizeOperation=0;return true;}
VKAPI_ATTR void* VKAPI_CALL resizeAllocate(void*,size_t,size_t,VkSystemAllocationScope){std::lock_guard lock(mutex);++resizeStats.allocations;return nullptr;}
VKAPI_ATTR void* VKAPI_CALL resizeReallocate(void*,void*,size_t,size_t,VkSystemAllocationScope){std::lock_guard lock(mutex);++resizeStats.allocations;return nullptr;}
VKAPI_ATTR void VKAPI_CALL resizeFree(void*,void*){}
const VkAllocationCallbacks resizeAllocator{nullptr,resizeAllocate,resizeReallocate,resizeFree,nullptr,nullptr};
std::map<VkSwapchainKHR,bool> customAllocators;
VKAPI_ATTR VkResult VKAPI_CALL resizeCreate(VkDevice d,const VkSwapchainCreateInfoKHR* info,const VkAllocationCallbacks* a,VkSwapchainKHR* out){
    {std::lock_guard lock(mutex);++resizeStats.creates;if(info->oldSwapchain){++resizeStats.oldAttempts;auto it=resizeChains.find(info->oldSwapchain);if(it==resizeChains.end()||it->second)++resizeStats.orderErrors;else it->second=true;}else ++resizeStats.freshAttempts;}
    const bool fail=consumeResize(resizeprobe::FailAllocation);
    auto r=createChain(d,info,fail?&resizeAllocator:a,out);
    {std::lock_guard lock(mutex);if(r==VK_SUCCESS){resizeChains[*out]=false;if(fail)customAllocators[*out]=true;}else ++resizeStats.nativeFailures;}
    return r;
}
VKAPI_ATTR void VKAPI_CALL resizeDestroy(VkDevice d,VkSwapchainKHR s,const VkAllocationCallbacks* a){
    bool custom;{std::lock_guard lock(mutex);custom=customAllocators.erase(s)!=0;resizeChains.erase(s);}
    lifetimeDestroyChain(d,s,custom?&resizeAllocator:a);
}
VKAPI_ATTR VkResult VKAPI_CALL resizeImages(VkDevice d,VkSwapchainKHR s,uint32_t* count,VkImage* images){
    if(consumeResize(resizeprobe::FailEnumeration)){std::lock_guard lock(mutex);++resizeStats.enumFailures;return VK_ERROR_OUT_OF_HOST_MEMORY;}
    return lifetimeImages(d,s,count,images);
}
VKAPI_ATTR VkResult VKAPI_CALL resizeAcquire(VkDevice d,VkSwapchainKHR s,uint64_t timeout,VkSemaphore semaphore,VkFence fence,uint32_t* index){
    {std::lock_guard lock(mutex);auto it=resizeChains.find(s);if(it==resizeChains.end()||it->second)++resizeStats.orderErrors;}
    if(consumeResize(resizeprobe::AcquireOutOfDate)){std::lock_guard lock(mutex);++resizeStats.acquireOutOfDate;return VK_ERROR_OUT_OF_DATE_KHR;}
    auto r=acquire(d,s,timeout,semaphore,fence,index);
    if((r==VK_SUCCESS||r==VK_SUBOPTIMAL_KHR)&&consumeResize(resizeprobe::AcquireSuboptimal)){std::lock_guard lock(mutex);++resizeStats.acquireSuboptimal;return VK_SUBOPTIMAL_KHR;}
    return r;
}
VKAPI_ATTR VkResult VKAPI_CALL resizePresent(VkQueue q,const VkPresentInfoKHR* info){
    auto r=lifetimePresent(q,info);
    // Forward first: the actual driver must consume the semaphore and signal the
    // actual present fence, including when the delivered result is overridden.
    if(r==VK_SUCCESS||r==VK_SUBOPTIMAL_KHR){
        if(consumeResize(resizeprobe::PresentOutOfDate)){std::lock_guard lock(mutex);++resizeStats.presentOutOfDate;return VK_ERROR_OUT_OF_DATE_KHR;}
        if(consumeResize(resizeprobe::PresentSuboptimal)){std::lock_guard lock(mutex);++resizeStats.presentSuboptimal;return VK_SUBOPTIMAL_KHR;}
    }
    return r;
}
PFN_vkVoidFunction resizeHook(const char* name){
#define R(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    R(CreateSwapchainKHR,resizeCreate) R(DestroySwapchainKHR,resizeDestroy) R(GetSwapchainImagesKHR,resizeImages)
    R(AcquireNextImageKHR,resizeAcquire) R(QueuePresentKHR,resizePresent)
#undef R
    return nullptr;
}
