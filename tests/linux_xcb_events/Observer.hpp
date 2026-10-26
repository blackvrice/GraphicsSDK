eventprobe::Snapshot eventStats;unsigned eventOperation{};bool closedBoundary{};
int eventControl(unsigned op){std::lock_guard lock(mutex);if(op>4)return 0;if(op==eventprobe::ClosedBoundary)closedBoundary=true;else if(!op)closedBoundary=false;else eventOperation=op;return 1;}
bool consumeEvent(unsigned op){std::lock_guard lock(mutex);if(eventOperation!=op)return false;eventOperation=0;return true;}
void eventBoundary(){std::lock_guard lock(mutex);if(closedBoundary)++eventStats.afterClose;}
VKAPI_ATTR VkResult VKAPI_CALL eventAcquire(VkDevice d,VkSwapchainKHR s,uint64_t timeout,VkSemaphore semaphore,VkFence fence,uint32_t* index){eventBoundary();if(consumeEvent(eventprobe::AcquireLost)){std::lock_guard lock(mutex);++eventStats.acquireLost;return VK_ERROR_SURFACE_LOST_KHR;}return acquire(d,s,timeout,semaphore,fence,index);}
VKAPI_ATTR VkResult VKAPI_CALL eventPresent(VkQueue q,const VkPresentInfoKHR* info){eventBoundary();auto r=lifetimePresent(q,info);if((r==VK_SUCCESS||r==VK_SUBOPTIMAL_KHR)&&consumeEvent(eventprobe::PresentLost)){std::lock_guard lock(mutex);++eventStats.presentLost;return VK_ERROR_SURFACE_LOST_KHR;}return r;}
VKAPI_ATTR VkResult VKAPI_CALL eventSubmit(VkQueue q,uint32_t n,const VkSubmitInfo* info,VkFence fence){eventBoundary();return lifetimeSubmit(q,n,info,fence);}
VKAPI_ATTR VkResult VKAPI_CALL eventCapabilities(VkPhysicalDevice p,VkSurfaceKHR s,VkSurfaceCapabilitiesKHR* out){eventBoundary();{std::lock_guard lock(mutex);++eventStats.queries;}if(consumeEvent(eventprobe::QueryLost)){std::lock_guard lock(mutex);++eventStats.queryLost;return VK_ERROR_SURFACE_LOST_KHR;}return next<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(parent(p),"vkGetPhysicalDeviceSurfaceCapabilitiesKHR")(p,s,out);}
PFN_vkVoidFunction eventHook(const char* name){
#define E(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    E(AcquireNextImageKHR,eventAcquire) E(QueuePresentKHR,eventPresent) E(QueueSubmit,eventSubmit) E(GetPhysicalDeviceSurfaceCapabilitiesKHR,eventCapabilities)
#undef E
    return nullptr;
}
