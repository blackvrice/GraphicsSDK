// Isolated test layer: synthetic errors and guarded invalid handles never reach the driver.
#define VK_NO_PROTOTYPES
#include <vulkan/vk_layer.h>
#include "Accounting.hpp"
#include "FaultPlan.hpp"
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

namespace {
using namespace trace;
void* key(const void* h){return *static_cast<void*const*>(h);}
template<class T>std::uint64_t id(T h){return reinterpret_cast<std::uint64_t>(h);}
struct Instance {VkInstance handle{};PFN_vkGetInstanceProcAddr get{};PFN_GetPhysicalDeviceProcAddr physical{};};
struct Device {
    VkDevice handle{};PFN_vkGetDeviceProcAddr get{};Ledger ledger;
    struct Binding {std::uint64_t memory{},offset{},size{};};
    std::map<std::uint64_t,VkMemoryRequirements> requirements;
    std::map<std::uint64_t,unsigned> types;
    std::map<std::uint64_t,Binding> bindings;
#define F(name) PFN_vk##name name{};
    F(GetBufferMemoryRequirements) F(DestroyDevice) F(CreateBuffer) F(DestroyBuffer) F(CreateImage) F(DestroyImage) F(AllocateMemory) F(FreeMemory) F(BindBufferMemory) F(BindImageMemory)
#undef F
};
struct Frame {unsigned phase{},iteration{};Counts counts;Live before,after;};
struct State {
    std::mutex mutex;std::map<void*,Instance> instances;std::map<void*,std::shared_ptr<Device>> devices;
    fault::Plan plan;
    struct Injection {unsigned iteration{};Call call{};int result{};std::uint64_t bytes{};bool poisoned{};};
    std::vector<Injection> injections;std::uint64_t invalidFrees{},guards{};
    Counts outside;std::vector<Frame> frames;unsigned phase{};std::uint64_t errors{},deviceCleanupResources{},deviceCleanupBytes{};
    std::atomic<unsigned> dropped{};std::string output;
    Live live(){Live l;for(auto& [k,d]:devices){(void)k;auto s=d->ledger.live();l.memories+=s.memories;l.bytes+=s.bytes;l.buffers+=s.buffers;l.images+=s.images;}return l;}
} g;
std::shared_ptr<Device> device(VkDevice d){std::lock_guard lock(g.mutex);auto it=g.devices.find(key(d));if(it==g.devices.end())std::abort();return it->second;}
void event(Device& d,Call c,bool ok,std::uint64_t handle=0,std::uint64_t size=0)noexcept{
    try{std::lock_guard lock(g.mutex);d.ledger.event(g.phase?g.frames.back().counts:g.outside,c,ok,handle,size);}catch(...){++g.dropped;}
}
int inject(Call call,std::uint64_t bytes=0){
    std::lock_guard lock(g.mutex);int result=call==AllocateMemory?g.plan.allocate():g.plan.bind();
    if(result)g.injections.push_back({g.frames.back().iteration,call,result,bytes,call==AllocateMemory&&g.plan.poison});return result;
}
VKAPI_ATTR VkResult VKAPI_CALL createBuffer(VkDevice d,const VkBufferCreateInfo*i,const VkAllocationCallbacks*a,VkBuffer*b){
    auto p=device(d);auto r=p->CreateBuffer(d,i,a,b);event(*p,CreateBuffer,r==VK_SUCCESS,r==VK_SUCCESS?id(*b):0);
    if(r==VK_SUCCESS){VkMemoryRequirements req{};p->GetBufferMemoryRequirements(d,*b,&req);std::lock_guard lock(g.mutex);p->requirements[id(*b)]=req;}return r;
}
VKAPI_ATTR void VKAPI_CALL destroyBuffer(VkDevice d,VkBuffer b,const VkAllocationCallbacks*a){
    auto p=device(d);{std::lock_guard lock(g.mutex);if(b&&!p->ledger.buffers.contains(id(b))){++g.guards;return;}p->bindings.erase(id(b));p->requirements.erase(id(b));}
    p->DestroyBuffer(d,b,a);event(*p,DestroyBuffer,true,id(b));
}
VKAPI_ATTR VkResult VKAPI_CALL createImage(VkDevice d,const VkImageCreateInfo*i,const VkAllocationCallbacks*a,VkImage*b){auto p=device(d);auto r=p->CreateImage(d,i,a,b);event(*p,CreateImage,r==VK_SUCCESS,r==VK_SUCCESS?id(*b):0);return r;}
VKAPI_ATTR void VKAPI_CALL destroyImage(VkDevice d,VkImage b,const VkAllocationCallbacks*a){auto p=device(d);p->DestroyImage(d,b,a);event(*p,DestroyImage,true,id(b));}
VKAPI_ATTR VkResult VKAPI_CALL allocateMemory(VkDevice d,const VkMemoryAllocateInfo*i,const VkAllocationCallbacks*a,VkDeviceMemory*b){
    auto p=device(d);auto failure=inject(AllocateMemory,i->allocationSize);
    if(failure){*b=g.plan.poison?reinterpret_cast<VkDeviceMemory>(std::uintptr_t(0xf11)):VK_NULL_HANDLE;event(*p,AllocateMemory,false);return static_cast<VkResult>(failure);}
    auto r=p->AllocateMemory(d,i,a,b);event(*p,AllocateMemory,r==VK_SUCCESS,r==VK_SUCCESS?id(*b):0,i->allocationSize);
    if(r==VK_SUCCESS){std::lock_guard lock(g.mutex);p->types[id(*b)]=i->memoryTypeIndex;}return r;
}
VKAPI_ATTR void VKAPI_CALL freeMemory(VkDevice d,VkDeviceMemory b,const VkAllocationCallbacks*a){
    auto p=device(d);bool invalid{};
    {std::lock_guard lock(g.mutex);
        if(b&&!p->ledger.memories.contains(id(b))){++g.invalidFrees;invalid=true;}
        for(auto& [handle,binding]:p->bindings)if(binding.memory==id(b)){(void)handle;++g.guards;invalid=true;}
        if(!invalid)p->types.erase(id(b));
    }
    if(invalid){event(*p,FreeMemory,false);return;} // Prevent deliberately poisoned test output reaching VVL/driver.
    p->FreeMemory(d,b,a);event(*p,FreeMemory,true,id(b));
}
VKAPI_ATTR VkResult VKAPI_CALL bindBuffer(VkDevice d,VkBuffer b,VkDeviceMemory m,VkDeviceSize o){
    auto p=device(d);auto failure=inject(BindBufferMemory,o);
    if(failure){event(*p,BindBufferMemory,false);return static_cast<VkResult>(failure);}
    bool invalid{};VkMemoryRequirements req{};
    {std::lock_guard lock(g.mutex);auto memory=p->ledger.memories.find(id(m));auto buffer=p->requirements.find(id(b));
        if(memory==p->ledger.memories.end()||buffer==p->requirements.end()||p->bindings.contains(id(b)))invalid=true;
        else{req=buffer->second;auto type=p->types.at(id(m));
            invalid=o>memory->second||req.size>memory->second-o||o%req.alignment||!(req.memoryTypeBits&(1u<<type));
            for(auto& [handle,binding]:p->bindings)if(binding.memory==id(m)&&o<binding.offset+binding.size&&binding.offset<o+req.size){(void)handle;invalid=true;}
        }
        if(invalid)++g.guards;
    }
    if(invalid){event(*p,BindBufferMemory,false);return VK_ERROR_UNKNOWN;}
    auto r=p->BindBufferMemory(d,b,m,o);event(*p,BindBufferMemory,r==VK_SUCCESS);
    if(r==VK_SUCCESS){std::lock_guard lock(g.mutex);p->bindings[id(b)]={id(m),o,req.size};}return r;
}
VKAPI_ATTR VkResult VKAPI_CALL bindImage(VkDevice d,VkImage b,VkDeviceMemory m,VkDeviceSize o){auto p=device(d);auto r=p->BindImageMemory(d,b,m,o);event(*p,BindImageMemory,r==VK_SUCCESS);return r;}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice d,const VkAllocationCallbacks*a){
    auto p=device(d);auto k=key(d);p->DestroyDevice(d,a);std::lock_guard lock(g.mutex);auto l=p->ledger.live();g.deviceCleanupResources+=l.memories+l.buffers+l.images;g.deviceCleanupBytes+=l.bytes;g.devices.erase(k);
}
VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance i,const VkAllocationCallbacks*a){
    Instance p;void*k=key(i);{std::lock_guard lock(g.mutex);p=g.instances.at(k);}
    reinterpret_cast<PFN_vkDestroyInstance>(p.get(i,"vkDestroyInstance"))(i,a);std::lock_guard lock(g.mutex);g.instances.erase(k);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance,const char*);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice,const char*);
VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo*i,const VkAllocationCallbacks*a,VkInstance*out){
    auto link=reinterpret_cast<const VkLayerInstanceCreateInfo*>(i->pNext);
    while(link&&(link->sType!=VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO||link->function!=VK_LAYER_LINK_INFO))link=reinterpret_cast<const VkLayerInstanceCreateInfo*>(link->pNext);
    if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    auto next=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;auto phys=link->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;
    const_cast<VkLayerInstanceCreateInfo*>(link)->u.pLayerInfo=link->u.pLayerInfo->pNext;
    auto r=reinterpret_cast<PFN_vkCreateInstance>(next(nullptr,"vkCreateInstance"))(i,a,out);
    if(r==VK_SUCCESS){try{std::lock_guard lock(g.mutex);g.instances.emplace(key(*out),Instance{*out,next,phys});}catch(...){std::abort();}}return r;
}
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo*i,const VkAllocationCallbacks*a,VkDevice*out){
    auto link=reinterpret_cast<const VkLayerDeviceCreateInfo*>(i->pNext);
    while(link&&(link->sType!=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO||link->function!=VK_LAYER_LINK_INFO))link=reinterpret_cast<const VkLayerDeviceCreateInfo*>(link->pNext);
    if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    auto next=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;auto gdpa=link->u.pLayerInfo->pfnNextGetDeviceProcAddr;Instance instance;
    {std::lock_guard lock(g.mutex);instance=g.instances.at(key(physical));}
    const_cast<VkLayerDeviceCreateInfo*>(link)->u.pLayerInfo=link->u.pLayerInfo->pNext;
    auto r=reinterpret_cast<PFN_vkCreateDevice>(next(instance.handle,"vkCreateDevice"))(physical,i,a,out);
    if(r==VK_SUCCESS){try{auto p=std::make_shared<Device>();p->handle=*out;p->get=gdpa;
#define F(name) p->name=reinterpret_cast<PFN_vk##name>(gdpa(*out,"vk" #name));if(!p->name)std::abort();
        F(GetBufferMemoryRequirements) F(DestroyDevice) F(CreateBuffer) F(DestroyBuffer) F(CreateImage) F(DestroyImage) F(AllocateMemory) F(FreeMemory) F(BindBufferMemory) F(BindImageMemory)
#undef F
        std::lock_guard lock(g.mutex);g.devices.emplace(key(*out),std::move(p));}catch(...){std::abort();}}return r;
}
PFN_vkVoidFunction hook(const char*n){
#define H(name,fn) if(!std::strcmp(n,"vk" #name))return reinterpret_cast<PFN_vkVoidFunction>(fn);
    H(GetInstanceProcAddr,getInstance) H(GetDeviceProcAddr,getDevice) H(CreateInstance,createInstance) H(DestroyInstance,destroyInstance) H(CreateDevice,createDevice) H(DestroyDevice,destroyDevice)
    H(CreateBuffer,createBuffer) H(DestroyBuffer,destroyBuffer) H(CreateImage,createImage) H(DestroyImage,destroyImage) H(AllocateMemory,allocateMemory) H(FreeMemory,freeMemory) H(BindBufferMemory,bindBuffer) H(BindImageMemory,bindImage)
#undef H
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance i,const char*n){
    if(auto h=hook(n))return h;if(!i)return nullptr;Instance p;{std::lock_guard lock(g.mutex);p=g.instances.at(key(i));}return p.get(i,n);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice d,const char*n){
    if(!std::strcmp(n,"vkGetInstanceProcAddr")||!std::strcmp(n,"vkCreateInstance")||!std::strcmp(n,"vkDestroyInstance")||!std::strcmp(n,"vkCreateDevice"))return nullptr;
    if(auto h=hook(n))return h;if(!d)return nullptr;auto p=device(d);return p->get(d,n);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getPhysical(VkInstance i,const char*n){if(!i)return nullptr;Instance p;{std::lock_guard lock(g.mutex);p=g.instances.at(key(i));}return p.physical?p.physical(i,n):p.get(i,n);}
void liveJson(std::ostream&o,Live l){o<<"{\"memories\":"<<l.memories<<",\"bytes\":"<<l.bytes<<",\"buffers\":"<<l.buffers<<",\"images\":"<<l.images<<'}';}
void countsJson(std::ostream&o,const Counts&c){o<<"{\"calls\":{";for(unsigned i=0;i<CallCount;++i){if(i)o<<',';o<<'"'<<names[i]<<"\":"<<c.calls[i];}o<<"},\"successes\":{";for(unsigned i=0;i<CallCount;++i){if(i)o<<',';o<<'"'<<names[i]<<"\":"<<c.successes[i];}o<<"},\"allocated_bytes\":"<<c.allocatedBytes<<",\"freed_bytes\":"<<c.freedBytes<<",\"errors\":"<<c.errors<<'}';}
}
extern "C" __declspec(dllexport) VKAPI_ATTR VkResult VKAPI_CALL gfxNegotiateLayer(VkNegotiateLayerInterface*v){
    if(!v||v->sType!=LAYER_NEGOTIATE_INTERFACE_STRUCT||v->loaderLayerInterfaceVersion<2)return VK_ERROR_INITIALIZATION_FAILED;
    v->loaderLayerInterfaceVersion=2;v->pfnGetInstanceProcAddr=getInstance;v->pfnGetDeviceProcAddr=getDevice;v->pfnGetPhysicalDeviceProcAddr=getPhysical;return VK_SUCCESS;
}
extern "C" __declspec(dllexport) int __cdecl gfxTracePhase(unsigned phase,unsigned iteration)noexcept{
    try{std::lock_guard lock(g.mutex);if(phase>6||iteration>256||g.frames.size()>=2048){++g.errors;return 0;}
        if(g.phase){g.frames.back().after=g.live();g.phase=0;}
        if(phase){g.frames.push_back(Frame{phase,iteration,{},g.live(),{}});g.phase=phase;}return 1;
    }catch(...){++g.dropped;return 0;}
}
extern "C" __declspec(dllexport) const char* __cdecl gfxTraceJson()noexcept{
    try{std::lock_guard lock(g.mutex);if(g.phase||g.dropped)return nullptr;std::ostringstream o;
        o<<"{\"schema_version\":1,\"source_fingerprint\":\""<<GFX_TRACE_FINGERPRINT<<"\",\"dropped_events\":"<<g.dropped<<",\"errors\":"<<g.errors<<",\"device_cleanup_resources\":"<<g.deviceCleanupResources<<",\"device_cleanup_bytes\":"<<g.deviceCleanupBytes<<",\"live\":";liveJson(o,g.live());o<<",\"outside\":";countsJson(o,g.outside);o<<",\"frames\":[";
        for(std::size_t j=0;j<g.frames.size();++j){if(j)o<<',';auto&f=g.frames[j];o<<"{\"phase\":"<<f.phase<<",\"iteration\":"<<f.iteration<<",\"before\":";liveJson(o,f.before);o<<",\"after\":";liveJson(o,f.after);o<<",\"counts\":";countsJson(o,f.counts);o<<'}';}o<<"],\"fault\":{\"pending\":"<<g.plan.pending()<<",\"invalid_frees\":"<<g.invalidFrees<<",\"guard_errors\":"<<g.guards<<",\"injections\":[";
        for(std::size_t j=0;j<g.injections.size();++j){if(j)o<<',';auto&e=g.injections[j];o<<"{\"iteration\":"<<e.iteration<<",\"call\":\""<<names[e.call]<<"\",\"result\":"<<e.result<<",\"bytes_or_offset\":"<<e.bytes<<",\"poisoned\":"<<(e.poisoned?"true":"false")<<'}';}
        o<<"]}}";g.output=o.str();return g.output.c_str();
    }catch(...){++g.dropped;return nullptr;}
}

extern "C" __declspec(dllexport) int __cdecl gfxFaultArm(int a,int b,int bind,unsigned poison)noexcept{
    std::lock_guard lock(g.mutex);return g.phase==2&&g.plan.arm(a,b,bind,poison)?1:0;
}
extern "C" __declspec(dllexport) unsigned __cdecl gfxFaultStatus()noexcept{
    std::lock_guard lock(g.mutex);return (g.invalidFrees?1u:0u)|(g.guards?2u:0u)|(g.plan.pending()?4u:0u);
}
