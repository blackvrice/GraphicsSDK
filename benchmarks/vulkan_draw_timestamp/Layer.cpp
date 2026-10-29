// Own pass-through layer. Only query reset and two GPU timestamp markers are added.
#define VK_NO_PROTOTYPES
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#define EXPORT extern "C" __declspec(dllexport)
namespace {
using Clock=std::chrono::steady_clock;
void* key(const void* h){return *static_cast<void*const*>(h);}
template<class T>std::uint64_t bits(T h){std::uint64_t n{};static_assert(sizeof(h)==sizeof(n));std::memcpy(&n,&h,sizeof(n));return n;}
template<class T>std::string hex(const T& bytes){std::string s;for(auto b:bytes){s+="0123456789abcdef"[(b>>4)&15];s+="0123456789abcdef"[b&15];}return s;}
std::string quote(const std::string& s){std::string o="\"";for(unsigned char c:s){if(c=='\\'||c=='\"')o+='\\';if(c<32)o+='?';else o+=char(c);}return o+'\"';}
std::string env(const char* name){char value[32768]{};auto n=GetEnvironmentVariableA(name,value,sizeof(value));return n&&n<sizeof(value)?std::string(value,n):std::string{};}
std::string modules(){HMODULE handles[1024]{};DWORD bytes{};std::ostringstream out;out<<'[';if(K32EnumProcessModules(GetCurrentProcess(),handles,sizeof(handles),&bytes)){unsigned written{};for(unsigned i=0;i<bytes/sizeof(HMODULE)&&i<1024;++i){wchar_t path[32768]{};auto n=GetModuleFileNameW(handles[i],path,32768);if(n&&n<32768){int size=WideCharToMultiByte(CP_UTF8,0,path,int(n),nullptr,0,nullptr,nullptr);std::string text(size,'\0');WideCharToMultiByte(CP_UTF8,0,path,int(n),text.data(),size,nullptr,nullptr);if(written++)out<<',';out<<quote(text);}}}out<<']';return out.str();}
struct Instance{VkInstance h;PFN_vkGetInstanceProcAddr get;PFN_GetPhysicalDeviceProcAddr physical;std::uint32_t api;VkDebugUtilsMessengerEXT controlMessenger{};};
struct Frame{
    VkCommandBuffer command{};VkQueryPool pool{};VkFence fence{};std::uint64_t commandBits{},queueBits{},fenceBits{};
    unsigned family{},queueIndex{},queueFlags{},validBits{},width{},height{},calls{},instances{},vertices{},badArguments{},scissors{},binds{};
    VkRect2D scissor{};bool ended{},submitted{},waitCompleted{},collected{},startBeforePass{},endAfterPass{};VkResult queryResult{VK_NOT_READY};
    std::array<std::uint64_t,2> ticks{},availability{};double gpuMs{},hostMs{};Clock::time_point submittedAt{};
};
struct Command{unsigned family{};std::size_t frame{SIZE_MAX};bool inPass{};};
struct Queue{unsigned family{},index{};};
struct Device{
    VkDevice h{};PFN_vkGetDeviceProcAddr get{};VkPhysicalDeviceProperties properties{};VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    std::uint32_t instanceApi{};std::vector<VkQueueFamilyProperties> families;std::map<VkCommandPool,unsigned> pools;std::map<VkCommandBuffer,Command> commands;std::map<VkQueue,Queue> queues;
    std::vector<Frame> frames;std::string fault;unsigned createdPools{},destroyedPools{};bool retired{};
    template<class T>T proc(const char* n){return reinterpret_cast<T>(get(h,n));}
    void fail(const char* text){if(fault.empty())fault=text;}
};
std::recursive_mutex mutex;std::map<void*,Instance> instances;std::map<void*,std::shared_ptr<Device>> devices;std::vector<std::shared_ptr<Device>> retired;
std::string mode,output;unsigned createdInstances{},destroyedInstances{};
std::vector<std::string> diagnostics;
VKAPI_ATTR VkBool32 VKAPI_CALL diagnostic(VkDebugUtilsMessageSeverityFlagBitsEXT,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*){std::lock_guard lock(mutex);std::string id=data&&data->pMessageIdName?data->pMessageIdName:"";diagnostics.push_back(id);std::cerr<<"GFX_TIMESTAMP_VALIDATION_DIAGNOSTIC|"<<id<<'\n';return VK_FALSE;}
Instance instance(const void* h){return instances.at(key(h));}
std::shared_ptr<Device> device(const void* h){return devices.at(key(h));}
bool enabled(){return mode!="audit";}
void report(){
    if(output.empty())return;std::ostringstream o;o<<std::setprecision(17)<<"{\"schema_version\":1,\"task\":\"P3-BEN-09\",\"layer_fingerprint\":"<<quote(GFX_TIMESTAMP_FINGERPRINT)<<",\"mode\":"<<quote(mode)<<",\"runtime_modules\":"<<modules()<<",\"live_instances\":"<<instances.size()<<",\"live_devices\":"<<devices.size()<<",\"created_instances\":"<<createdInstances<<",\"destroyed_instances\":"<<destroyedInstances<<",\"devices\":[";
    for(std::size_t i=0;i<retired.size();++i){if(i)o<<',';auto& d=*retired[i];o<<"{\"device_name\":"<<quote(d.properties.deviceName)<<",\"device_uuid\":"<<quote(hex(d.id.deviceUUID))<<",\"driver_uuid\":"<<quote(hex(d.id.driverUUID))<<",\"device_luid\":"<<quote(hex(d.id.deviceLUID))<<",\"luid_valid\":"<<(d.id.deviceLUIDValid?"true":"false")<<",\"vendor_id\":"<<d.properties.vendorID<<",\"device_id\":"<<d.properties.deviceID<<",\"driver_version\":"<<d.properties.driverVersion<<",\"api_version\":"<<d.properties.apiVersion<<",\"device_type\":"<<d.properties.deviceType<<",\"instance_api_version\":"<<d.instanceApi<<",\"timestamp_period_ns\":"<<d.properties.limits.timestampPeriod<<",\"native_device\":"<<quote(std::to_string(bits(d.h)))<<",\"retired\":"<<(d.retired?"true":"false")<<",\"fault\":"<<quote(d.fault)<<",\"query_pools_created\":"<<d.createdPools<<",\"query_pools_destroyed\":"<<d.destroyedPools<<",\"frames\":[";
        for(std::size_t j=0;j<d.frames.size();++j){if(j)o<<',';auto& f=d.frames[j];o<<"{\"index\":"<<j<<",\"command\":"<<quote(std::to_string(f.commandBits))<<",\"queue\":"<<quote(std::to_string(f.queueBits))<<",\"fence\":"<<quote(std::to_string(f.fenceBits))<<",\"queue_family\":"<<f.family<<",\"queue_index\":"<<f.queueIndex<<",\"queue_flags\":"<<f.queueFlags<<",\"timestamp_valid_bits\":"<<f.validBits<<",\"render_area\":[0,0,"<<f.width<<','<<f.height<<"],\"scissor\":["<<f.scissor.offset.x<<','<<f.scissor.offset.y<<','<<f.scissor.extent.width<<','<<f.scissor.extent.height<<"],\"draw_calls\":"<<f.calls<<",\"instances\":"<<f.instances<<",\"vertices\":"<<f.vertices<<",\"bad_arguments\":"<<f.badArguments<<",\"scissor_commands\":"<<f.scissors<<",\"pipeline_binds\":"<<f.binds<<",\"ended\":"<<(f.ended?"true":"false")<<",\"submitted\":"<<(f.submitted?"true":"false")<<",\"fence_completed\":"<<(f.waitCompleted?"true":"false")<<",\"collected\":"<<(f.collected?"true":"false")<<",\"start_before_render_pass\":"<<(f.startBeforePass?"true":"false")<<",\"end_after_render_pass\":"<<(f.endAfterPass?"true":"false")<<",\"query_result\":"<<(enabled()?std::to_string(f.queryResult):"null")<<",\"ticks\":";
            if(enabled())o<<'['<<quote(std::to_string(f.ticks[0]))<<','<<quote(std::to_string(f.ticks[1]))<<']';else o<<"null";
            o<<",\"availability\":";if(enabled())o<<'['<<f.availability[0]<<','<<f.availability[1]<<']';else o<<"null";
            o<<",\"gpu_ms\":";if(enabled()&&f.queryResult==VK_SUCCESS&&f.availability[0]&&f.availability[1])o<<f.gpuMs;else o<<"null";o<<",\"submit_to_completed_fence_ms\":"<<f.hostMs<<'}';}
        o<<"]}";}o<<"],\"validation_diagnostics\":[";for(std::size_t i=0;i<diagnostics.size();++i){if(i)o<<',';o<<quote(diagnostics[i]);}o<<"]}\n";std::ofstream file(output,std::ios::binary|std::ios::trunc);file<<o.str();
}
void collect(Device& d,Frame& f,Clock::time_point completed){
    f.waitCompleted=true;f.hostMs=std::chrono::duration<double,std::milli>(completed-f.submittedAt).count();
    if(f.pool){struct Value{std::uint64_t ticks,available;};Value values[2]{{UINT64_MAX,0},{UINT64_MAX,0}};
        f.queryResult=d.proc<PFN_vkGetQueryPoolResults>("vkGetQueryPoolResults")(d.h,f.pool,0,2,sizeof(values),values,sizeof(Value),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        for(unsigned i=0;i<2;++i){f.ticks[i]=values[i].ticks;f.availability[i]=values[i].available;}
        if(f.queryResult!=VK_SUCCESS||!f.availability[0]||!f.availability[1])d.fail("query_results_unavailable");
        else{auto mask=f.validBits==64?UINT64_MAX:(std::uint64_t{1}<<f.validBits)-1;auto delta=(f.ticks[1]-f.ticks[0])&mask;f.gpuMs=double(delta)*d.properties.limits.timestampPeriod/1e6;if(!f.startBeforePass||!f.endAfterPass)d.fail("boundary_mismatch");if(!delta||f.gpuMs>f.hostMs+0.001)d.fail("invalid_counter_elapsed");}
        d.proc<PFN_vkDestroyQueryPool>("vkDestroyQueryPool")(d.h,f.pool,nullptr);f.pool={};++d.destroyedPools;
    }f.collected=true;
}
VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance h,const VkAllocationCallbacks* a){std::lock_guard lock(mutex);auto p=instance(h);auto k=key(h);if(p.controlMessenger)reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(p.get(h,"vkDestroyDebugUtilsMessengerEXT"))(h,p.controlMessenger,nullptr);reinterpret_cast<PFN_vkDestroyInstance>(p.get(h,"vkDestroyInstance"))(h,a);instances.erase(k);++destroyedInstances;report();}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice h,const VkAllocationCallbacks* a){std::lock_guard lock(mutex);auto d=device(h);auto k=key(h);
    for(auto& f:d->frames)if(!f.collected){d->fail("uncollected_render_pass");d->proc<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(h);if(f.pool){d->proc<PFN_vkDestroyQueryPool>("vkDestroyQueryPool")(h,f.pool,nullptr);f.pool={};++d->destroyedPools;}}
    d->proc<PFN_vkDestroyDevice>("vkDestroyDevice")(h,a);d->retired=true;devices.erase(k);retired.push_back(d);report();
}
VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo* info,const VkAllocationCallbacks* a,VkInstance* out){std::lock_guard lock(mutex);
    mode=env("GFX_GPU_TIMESTAMP_MODE");output=env("GFX_GPU_TIMESTAMP_OUTPUT");if(output.empty()||(mode!="audit"&&mode!="timestamp"&&mode!="missing-end"&&mode!="wrong-boundary"&&mode!="validation-control"))return VK_ERROR_INITIALIZATION_FAILED;
    auto link=reinterpret_cast<const VkLayerInstanceCreateInfo*>(info->pNext);while(link&&(link->sType!=VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO||link->function!=VK_LAYER_LINK_INFO))link=reinterpret_cast<const VkLayerInstanceCreateInfo*>(link->pNext);if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    auto get=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;auto physical=link->u.pLayerInfo->pfnNextGetPhysicalDeviceProcAddr;const_cast<VkLayerInstanceCreateInfo*>(link)->u.pLayerInfo=link->u.pLayerInfo->pNext;auto result=reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"))(info,a,out);
    if(result==VK_SUCCESS){auto& p=instances.emplace(key(*out),Instance{*out,get,physical,info->pApplicationInfo?info->pApplicationInfo->apiVersion:0}).first->second;++createdInstances;
        if(mode=="validation-control"){VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;debug.pfnUserCallback=diagnostic;auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(get(*out,"vkCreateDebugUtilsMessengerEXT"));if(create)create(*out,&debug,nullptr,&p.controlMessenger);}
    }return result;
}
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice h,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* a,VkDevice* out){std::lock_guard lock(mutex);
    auto link=reinterpret_cast<const VkLayerDeviceCreateInfo*>(info->pNext);while(link&&(link->sType!=VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO||link->function!=VK_LAYER_LINK_INFO))link=reinterpret_cast<const VkLayerDeviceCreateInfo*>(link->pNext);if(!link||!link->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
    auto p=instance(h);auto next=link->u.pLayerInfo->pfnNextGetInstanceProcAddr;auto get=link->u.pLayerInfo->pfnNextGetDeviceProcAddr;const_cast<VkLayerDeviceCreateInfo*>(link)->u.pLayerInfo=link->u.pLayerInfo->pNext;auto result=reinterpret_cast<PFN_vkCreateDevice>(next(p.h,"vkCreateDevice"))(h,info,a,out);
    if(result==VK_SUCCESS){auto d=std::make_shared<Device>();d->h=*out;d->get=get;d->instanceApi=p.api;VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&d->id;auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(p.get(p.h,"vkGetPhysicalDeviceProperties2"));properties(h,&props);d->properties=props.properties;unsigned n{};auto families=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(p.get(p.h,"vkGetPhysicalDeviceQueueFamilyProperties"));families(h,&n,nullptr);d->families.resize(n);families(h,&n,d->families.data());
        if(mode=="validation-control"){
            // Host-only positive core/callback control. No command is recorded or submitted.
            VkPhysicalDeviceProperties2 invalid{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};properties(h,&invalid);d->fail("validation_control_only");d->proc<PFN_vkDestroyDevice>("vkDestroyDevice")(*out,a);d->retired=true;retired.push_back(d);*out={};report();return VK_ERROR_VALIDATION_FAILED_EXT;
        }devices.emplace(key(*out),d);}return result;
}
VKAPI_ATTR void VKAPI_CALL getQueue(VkDevice h,uint32_t family,uint32_t index,VkQueue* out){std::lock_guard lock(mutex);auto d=device(h);d->proc<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(h,family,index,out);d->queues[*out]={family,index};}
VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice h,const VkCommandPoolCreateInfo* info,const VkAllocationCallbacks* a,VkCommandPool* out){std::lock_guard lock(mutex);auto d=device(h);auto r=d->proc<PFN_vkCreateCommandPool>("vkCreateCommandPool")(h,info,a,out);if(r==VK_SUCCESS)d->pools[*out]=info->queueFamilyIndex;return r;}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice h,VkCommandPool pool,const VkAllocationCallbacks* a){std::lock_guard lock(mutex);auto d=device(h);d->proc<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(h,pool,a);d->pools.erase(pool);}
VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice h,const VkCommandBufferAllocateInfo* info,VkCommandBuffer* out){std::lock_guard lock(mutex);auto d=device(h);auto r=d->proc<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(h,info,out);if(r==VK_SUCCESS)for(unsigned i=0;i<info->commandBufferCount;++i)d->commands[out[i]]=Command{d->pools.at(info->commandPool)};return r;}
VKAPI_ATTR void VKAPI_CALL freeCommands(VkDevice h,VkCommandPool pool,uint32_t n,const VkCommandBuffer* commands){std::lock_guard lock(mutex);auto d=device(h);d->proc<PFN_vkFreeCommandBuffers>("vkFreeCommandBuffers")(h,pool,n,commands);for(unsigned i=0;i<n;++i)d->commands.erase(commands[i]);}
VKAPI_ATTR VkResult VKAPI_CALL beginCommand(VkCommandBuffer cmd,const VkCommandBufferBeginInfo* info){std::lock_guard lock(mutex);auto d=device(cmd);auto r=d->proc<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cmd,info);if(r==VK_SUCCESS){auto& c=d->commands.at(cmd);if(c.frame!=SIZE_MAX&&!d->frames[c.frame].collected)d->fail("command_reused_before_fence");c.frame=SIZE_MAX;c.inPass=false;}return r;}
VKAPI_ATTR void VKAPI_CALL beginPass(VkCommandBuffer cmd,const VkRenderPassBeginInfo* info,VkSubpassContents contents){std::lock_guard lock(mutex);auto d=device(cmd);auto& c=d->commands.at(cmd);Frame f{};f.command=cmd;f.commandBits=bits(cmd);f.family=c.family;f.width=info->renderArea.extent.width;f.height=info->renderArea.extent.height;
    if(c.family>=d->families.size()||!d->families[c.family].timestampValidBits||d->families[c.family].timestampValidBits>64){d->fail("timestamp_queue_unsupported");}else{f.validBits=d->families[c.family].timestampValidBits;f.queueFlags=d->families[c.family].queueFlags;}
    if(info->renderArea.offset.x||info->renderArea.offset.y)d->fail("render_area_offset");
    if(enabled()&&f.validBits){VkQueryPoolCreateInfo pool{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};pool.queryType=VK_QUERY_TYPE_TIMESTAMP;pool.queryCount=2;auto result=d->proc<PFN_vkCreateQueryPool>("vkCreateQueryPool")(d->h,&pool,nullptr,&f.pool);if(result!=VK_SUCCESS)d->fail("query_pool_create_failed");else{++d->createdPools;d->proc<PFN_vkCmdResetQueryPool>("vkCmdResetQueryPool")(cmd,f.pool,0,2);if(mode!="wrong-boundary"){d->proc<PFN_vkCmdWriteTimestamp>("vkCmdWriteTimestamp")(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,f.pool,0);f.startBeforePass=true;}}}
    c.frame=d->frames.size();c.inPass=true;d->frames.push_back(f);d->proc<PFN_vkCmdBeginRenderPass>("vkCmdBeginRenderPass")(cmd,info,contents);
}
VKAPI_ATTR void VKAPI_CALL endPass(VkCommandBuffer cmd){std::lock_guard lock(mutex);auto d=device(cmd);auto& c=d->commands.at(cmd);d->proc<PFN_vkCmdEndRenderPass>("vkCmdEndRenderPass")(cmd);if(c.frame==SIZE_MAX){d->fail("end_without_pass");return;}auto& f=d->frames[c.frame];f.ended=true;c.inPass=false;
    if(f.pool){if(mode=="wrong-boundary")d->proc<PFN_vkCmdWriteTimestamp>("vkCmdWriteTimestamp")(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,f.pool,0);if(mode!="missing-end"){d->proc<PFN_vkCmdWriteTimestamp>("vkCmdWriteTimestamp")(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,f.pool,1);f.endAfterPass=true;}}
}
VKAPI_ATTR void VKAPI_CALL scissor(VkCommandBuffer cmd,uint32_t first,uint32_t count,const VkRect2D* rects){std::lock_guard lock(mutex);auto d=device(cmd);auto& c=d->commands.at(cmd);if(c.inPass&&c.frame!=SIZE_MAX){auto& f=d->frames[c.frame];++f.scissors;if(first==0&&count==1)f.scissor=rects[0];else d->fail("scissor_count");}d->proc<PFN_vkCmdSetScissor>("vkCmdSetScissor")(cmd,first,count,rects);}
VKAPI_ATTR void VKAPI_CALL bind(VkCommandBuffer cmd,VkPipelineBindPoint point,VkPipeline pipeline){std::lock_guard lock(mutex);auto d=device(cmd);auto& c=d->commands.at(cmd);if(c.inPass&&c.frame!=SIZE_MAX&&point==VK_PIPELINE_BIND_POINT_GRAPHICS)++d->frames[c.frame].binds;d->proc<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(cmd,point,pipeline);}
VKAPI_ATTR void VKAPI_CALL draw(VkCommandBuffer cmd,uint32_t vertices,uint32_t count,uint32_t first,uint32_t instanceIndex){std::lock_guard lock(mutex);auto d=device(cmd);auto& c=d->commands.at(cmd);if(c.inPass&&c.frame!=SIZE_MAX){auto& f=d->frames[c.frame];++f.calls;f.instances+=count;f.vertices+=vertices*count;f.badArguments+=(vertices!=3||first!=0||instanceIndex!=0||f.scissor.offset.x!=5||f.scissor.offset.y!=3||f.scissor.extent.width!=55||f.scissor.extent.height!=31);}else d->fail("draw_outside_pass");d->proc<PFN_vkCmdDraw>("vkCmdDraw")(cmd,vertices,count,first,instanceIndex);}
VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue queue,uint32_t n,const VkSubmitInfo* infos,VkFence fence){std::lock_guard lock(mutex);auto d=device(queue);auto started=Clock::now();auto result=d->proc<PFN_vkQueueSubmit>("vkQueueSubmit")(queue,n,infos,fence);
    if(result==VK_SUCCESS)for(unsigned i=0;i<n;++i)for(unsigned j=0;j<infos[i].commandBufferCount;++j){auto it=d->commands.find(infos[i].pCommandBuffers[j]);if(it==d->commands.end()||it->second.frame==SIZE_MAX)continue;auto& f=d->frames[it->second.frame];if(f.collected)continue;auto q=d->queues.find(queue);if(q==d->queues.end()||q->second.family!=f.family){d->fail("actual_queue_binding_mismatch");continue;}if(!fence)d->fail("render_submit_without_fence");f.submitted=true;f.queueBits=bits(queue);f.fence=fence;f.fenceBits=bits(fence);f.queueIndex=q->second.index;f.submittedAt=started;}return result;
}
VKAPI_ATTR VkResult VKAPI_CALL wait(VkDevice h,uint32_t n,const VkFence* fences,VkBool32 all,uint64_t timeout){std::lock_guard lock(mutex);auto d=device(h);auto result=d->proc<PFN_vkWaitForFences>("vkWaitForFences")(h,n,fences,all,timeout);auto completed=Clock::now();if(result==VK_SUCCESS)for(auto& f:d->frames)if(f.submitted&&!f.collected)for(unsigned i=0;i<n;++i)if(f.fence==fences[i]&&d->proc<PFN_vkGetFenceStatus>("vkGetFenceStatus")(h,fences[i])==VK_SUCCESS){collect(*d,f,completed);break;}return result;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance,const char*);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice,const char*);
PFN_vkVoidFunction hook(const char* name){
#define H(n,f) if(!std::strcmp(name,"vk" #n))return reinterpret_cast<PFN_vkVoidFunction>(f);
    H(GetInstanceProcAddr,getInstance) H(GetDeviceProcAddr,getDevice) H(CreateInstance,createInstance) H(DestroyInstance,destroyInstance) H(CreateDevice,createDevice) H(DestroyDevice,destroyDevice) H(GetDeviceQueue,getQueue)
    H(CreateCommandPool,createPool) H(DestroyCommandPool,destroyPool) H(AllocateCommandBuffers,allocate) H(FreeCommandBuffers,freeCommands) H(BeginCommandBuffer,beginCommand) H(CmdBeginRenderPass,beginPass) H(CmdEndRenderPass,endPass) H(CmdSetScissor,scissor) H(CmdBindPipeline,bind) H(CmdDraw,draw) H(QueueSubmit,submit) H(WaitForFences,wait)
#undef H
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstance(VkInstance h,const char* name){std::lock_guard lock(mutex);if(auto p=hook(name))return p;return h?instance(h).get(h,name):nullptr;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDevice(VkDevice h,const char* name){std::lock_guard lock(mutex);if(auto p=hook(name))return p;return h?device(h)->get(h,name):nullptr;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getPhysical(VkInstance h,const char* name){std::lock_guard lock(mutex);if(!h)return nullptr;auto p=instance(h);return p.physical?p.physical(h,name):p.get(h,name);}
}
EXPORT VKAPI_ATTR VkResult VKAPI_CALL gfxTimestampNegotiate(VkNegotiateLayerInterface* value){if(!value||value->sType!=LAYER_NEGOTIATE_INTERFACE_STRUCT||value->loaderLayerInterfaceVersion<2)return VK_ERROR_INITIALIZATION_FAILED;value->loaderLayerInterfaceVersion=2;value->pfnGetInstanceProcAddr=getInstance;value->pfnGetDeviceProcAddr=getDevice;value->pfnGetPhysicalDeviceProcAddr=getPhysical;return VK_SUCCESS;}
