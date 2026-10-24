// Reuse the immutable own fault layer; expose read-only binding metadata to the
// new independent installed-SDK consumer. No production hook is added.
#include "../../experiments/vulkan_buffer_pool_fault/Layer.cpp"
#include "Snapshot.hpp"
extern "C" __declspec(dllexport) int __cdecl gfxChurnSnapshot(churn::Snapshot*out,unsigned size)noexcept{
    if(!out||size!=sizeof(*out))return 0;
    try{
        std::lock_guard lock(g.mutex);*out={};auto live=g.live();out->memories=live.memories;out->bytes=live.bytes;out->buffers=live.buffers;out->images=live.images;
        for(auto&[key,p]:g.devices){(void)key;
            auto query=reinterpret_cast<PFN_vkGetBufferMemoryRequirements2>(p->get(p->handle,"vkGetBufferMemoryRequirements2"));if(!query)return 0;
            for(auto&[handle,b]:p->bindings){
                if(out->count>=churn::limit)return 0;
                VkMemoryDedicatedRequirements dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
                VkMemoryRequirements2 req{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};req.pNext=&dedicated;
                VkBufferMemoryRequirementsInfo2 info{VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2};info.buffer=reinterpret_cast<VkBuffer>(handle);query(p->handle,&info,&req);
                auto old=p->requirements.at(handle);if(req.memoryRequirements.size!=old.size||req.memoryRequirements.alignment!=old.alignment||req.memoryRequirements.memoryTypeBits!=old.memoryTypeBits)return 0;
                out->entries[out->count++]={handle,b.memory,b.offset,old.size,old.alignment,p->ledger.memories.at(b.memory),p->types.at(b.memory),old.memoryTypeBits,unsigned(dedicated.prefersDedicatedAllocation||dedicated.requiresDedicatedAllocation)};
            }
        }
        return out->count==out->buffers?1:0;
    }catch(...){return 0;}
}
