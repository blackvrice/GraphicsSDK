"""Derive a separate fault layer from own frozen diagnostic code. No old file edits."""
import pathlib
root=pathlib.Path(__file__).resolve().parents[5];out=root/'experiments/vulkan_buffer_pool_fault'
def patch(s,a,b):
    assert s.count(a)==1,a[:100]
    return s.replace(a,b)
def write(n,s):(out/n).write_bytes(s.replace('\r\n','\n').replace('\n','\r\n').encode())
write('Accounting.hpp',(root/'benchmarks/vulkan_allocation_trace/Accounting.hpp').read_text(encoding='utf-8'))
s=(root/'benchmarks/vulkan_allocation_trace/Layer.cpp').read_text(encoding='utf-8')
s=s.replace('// Process-local diagnostic layer. No resource wrapping, settings or driver changes.','// Isolated test layer: synthetic errors and guarded invalid handles never reach the driver.')
s=s.replace('#include "Accounting.hpp"','#include "Accounting.hpp"\n#include "FaultPlan.hpp"')
s=patch(s,'VkDevice handle{};PFN_vkGetDeviceProcAddr get{};Ledger ledger;','''VkDevice handle{};PFN_vkGetDeviceProcAddr get{};Ledger ledger;
    struct Binding {std::uint64_t memory{},offset{},size{};};
    std::map<std::uint64_t,VkMemoryRequirements> requirements;
    std::map<std::uint64_t,unsigned> types;
    std::map<std::uint64_t,Binding> bindings;''')
s=s.replace('F(DestroyDevice) F(CreateBuffer)','F(GetBufferMemoryRequirements) F(DestroyDevice) F(CreateBuffer)')
s=patch(s,'Counts outside;std::vector<Frame> frames;','''fault::Plan plan;
    struct Injection {unsigned iteration{};Call call{};int result{};std::uint64_t bytes{};bool poisoned{};};
    std::vector<Injection> injections;std::uint64_t invalidFrees{},guards{};
    Counts outside;std::vector<Frame> frames;''')
a='VKAPI_ATTR VkResult VKAPI_CALL createBuffer('
start=s.index(a);end=s.index('VKAPI_ATTR VkResult VKAPI_CALL createImage(',start)
s=s[:start]+'''int inject(Call call,std::uint64_t bytes=0){
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
'''+s[end:]
start=s.index('VKAPI_ATTR VkResult VKAPI_CALL allocateMemory(');end=s.index('VKAPI_ATTR VkResult VKAPI_CALL bindImage(',start)
s=s[:start]+'''VKAPI_ATTR VkResult VKAPI_CALL allocateMemory(VkDevice d,const VkMemoryAllocateInfo*i,const VkAllocationCallbacks*a,VkDeviceMemory*b){
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
'''+s[end:]
s=patch(s,'o<<"]}";g.output=o.str();return g.output.c_str();','''o<<"],\\"fault\\":{\\"pending\\":"<<g.plan.pending()<<",\\"invalid_frees\\":"<<g.invalidFrees<<",\\"guard_errors\\":"<<g.guards<<",\\"injections\\":[";
        for(std::size_t j=0;j<g.injections.size();++j){if(j)o<<',';auto&e=g.injections[j];o<<"{\\"iteration\\":"<<e.iteration<<",\\"call\\":\\""<<names[e.call]<<"\\",\\"result\\":"<<e.result<<",\\"bytes_or_offset\\":"<<e.bytes<<",\\"poisoned\\":"<<(e.poisoned?"true":"false")<<'}';}
        o<<"]}}";g.output=o.str();return g.output.c_str();''')
s+='''
extern "C" __declspec(dllexport) int __cdecl gfxFaultArm(int a,int b,int bind,unsigned poison)noexcept{
    std::lock_guard lock(g.mutex);return g.phase==2&&g.plan.arm(a,b,bind,poison)?1:0;
}
extern "C" __declspec(dllexport) unsigned __cdecl gfxFaultStatus()noexcept{
    std::lock_guard lock(g.mutex);return (g.invalidFrees?1u:0u)|(g.guards?2u:0u)|(g.plan.pending()?4u:0u);
}
'''
write('Layer.cpp',s)
s=(root/'benchmarks/vulkan_allocation_trace/Client.hpp').read_text(encoding='utf-8').replace('VkLayer_gfx_allocation.dll','VkLayer_gfx_buffer_fault.dll').replace('GFX_EXPECT_TRACE_LAYER','GFX_EXPECT_FAULT_LAYER')
s=s.replace('HMODULE module{};Phase phase{};Json json{};','using Arm=int(__cdecl*)(int,int,int,unsigned);using Status=unsigned(__cdecl*)();\n    HMODULE module{};Phase phase{};Json json{};Arm arm{};Status status{};')
s=s.replace('if(!phase||!json)','arm=reinterpret_cast<Arm>(GetProcAddress(module,"gfxFaultArm"));status=reinterpret_cast<Status>(GetProcAddress(module,"gfxFaultStatus"));\n        if(!phase||!json||!arm||!status)')
write('Client.hpp',s)
print('PASS separate guarded fault layer/client generated')
