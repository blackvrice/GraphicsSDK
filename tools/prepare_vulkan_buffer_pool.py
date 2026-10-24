"""Make a reviewed, isolated SDK candidate; never patch product or vendor sources."""
import argparse
import hashlib
import json
import pathlib
import shutil
import audit_licenses as audit
ROOT=pathlib.Path(__file__).resolve().parents[1]
BASE='44b92cdf3a4ae01c97af10fe25d57b6dea487bf6ed7826a7f322e338a1d39e25'
def patch(s,a,b):
    if s.count(a)!=1:raise ValueError('Candidate anchor changed: '+a[:80])
    return s.replace(a,b)
def prepare(destination):
    dst=destination.resolve()
    if not dst.is_relative_to(ROOT/'out') or dst.exists():raise ValueError('New isolated out/ source root required')
    if audit.sdk_fingerprint(ROOT)!=BASE:raise ValueError('Reviewed product SDK source identity changed')
    dst.mkdir(parents=True)
    for folder in ('include','src','cmake','tests','examples','tools','shaders'):shutil.copytree(ROOT/folder,dst/folder,ignore=shutil.ignore_patterns('__pycache__'))
    for n in ('CMakeLists.txt','LICENSE'):shutil.copyfile(ROOT/n,dst/n)
    def write(n,s):(dst/n).write_bytes(s.replace('\r\n','\n').replace('\n','\r\n').encode())
    s=(ROOT/'src/backends/vulkan/Vulkan.cpp').read_text(encoding='utf-8')
    s=patch(s,'#include "Timestamp.hpp"','#include "Timestamp.hpp"\n#include "BufferPoolRange.hpp"\n#include <mutex>\n#include <set>')
    s=patch(s,'X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements)','X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements2) X(GetBufferMemoryRequirements)')
    s=patch(s,'struct NativeSurface {','#include "BufferPool.hpp"\nstruct NativeSurface {')
    s=patch(s,'    VkDeviceMemory memory{};\n    VkDeviceSize bytes{};','    VkDeviceMemory memory{};\n    std::shared_ptr<BufferBlock> block;\n    std::uint32_t slot{};\n    VkDeviceSize bytes{};')
    s=patch(s,'        if (memory) native->FreeMemory(native->handle, memory, nullptr);','        if (block) { block->give(slot); block.reset(); }\n        else if (memory) native->FreeMemory(native->handle, memory, nullptr);')
    s=patch(s,'    std::vector<std::weak_ptr<Allocation>> allocations;','    std::vector<std::weak_ptr<Allocation>> allocations;\n    std::vector<std::weak_ptr<BufferBlock>> bufferBlocks;')
    s=patch(s,'        for (auto& w : owner->allocations) if (auto a = w.lock()) { ++result.uniqueResources; result.resourceAllocationBytes += a->bytes; }','''        std::set<const BufferBlock*> counted;
        for (auto& w : owner->allocations) if (auto a = w.lock()) {
            ++result.uniqueResources;
            if (a->block) { if(counted.insert(a->block.get()).second)result.resourceAllocationBytes+=vulkan::BufferSlots::blockBytes; }
            else result.resourceAllocationBytes+=a->bytes;
        }''')
    s=patch(s,'        VkMemoryRequirements req{}; owner->native->GetBufferMemoryRequirements(owner->native->handle, a->buffer, &req);','''        VkMemoryDedicatedRequirements dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
        VkMemoryRequirements2 requirements{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};requirements.pNext=&dedicated;
        VkBufferMemoryRequirementsInfo2 query{VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2};query.buffer=a->buffer;
        owner->native->GetBufferMemoryRequirements2(owner->native->handle,&query,&requirements);
        const auto req=requirements.memoryRequirements;''')
    a='''        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocate.allocationSize = req.size; allocate.memoryTypeIndex = *type;
        if (auto ok = owner->accept(owner->native->AllocateMemory(owner->native->handle, &allocate, nullptr, &a->memory), "vkAllocateMemory"); !ok) return std::unexpected(ok.error());
        if (auto ok = owner->accept(owner->native->BindBufferMemory(owner->native->handle, a->buffer, a->memory, 0), "vkBindBufferMemory"); !ok) return std::unexpected(ok.error());'''
    b='''        VkDeviceSize bindOffset{};
        const auto stride=desc.memory==Memory::Device?vulkan::BufferSlots::stride(req.size,req.alignment,dedicated.requiresDedicatedAllocation||dedicated.prefersDedicatedAllocation):0;
        if(stride){
            std::erase_if(owner->bufferBlocks,[](auto&w){return w.expired();});
            for(auto&w:owner->bufferBlocks)if(auto b=w.lock();b&&b->type==*type&&b->slots.stride()==stride)if(auto slot=b->take()){a->block=b;a->slot=*slot;break;}
            if(!a->block){
                auto block=std::make_shared<BufferBlock>(owner->native,*type,stride);
                VkMemoryAllocateInfo pool{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};pool.allocationSize=vulkan::BufferSlots::blockBytes;pool.memoryTypeIndex=*type;
                auto r=owner->native->AllocateMemory(owner->native->handle,&pool,nullptr,&block->memory);
                if(r==VK_SUCCESS){a->slot=*block->take();a->block=block;owner->bufferBlocks.push_back(block);}
                else {block->memory={};if(r!=VK_ERROR_OUT_OF_HOST_MEMORY&&r!=VK_ERROR_OUT_OF_DEVICE_MEMORY){if(auto ok=owner->accept(r,"vkAllocateMemory pool");!ok)return std::unexpected(ok.error());}}
                // On a larger block OOM, retain the original exact-size fallback path.
            }
            if(a->block){a->memory=a->block->memory;bindOffset=VkDeviceSize(a->slot)*stride;}
        }
        if(!a->memory){
            VkMemoryDedicatedAllocateInfo dedicatedInfo{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicatedInfo.buffer=a->buffer;
            VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocate.allocationSize=req.size;allocate.memoryTypeIndex=*type;
            if(dedicated.requiresDedicatedAllocation||dedicated.prefersDedicatedAllocation)allocate.pNext=&dedicatedInfo;
            if(auto ok=owner->accept(owner->native->AllocateMemory(owner->native->handle,&allocate,nullptr,&a->memory),"vkAllocateMemory");!ok)return std::unexpected(ok.error());
        }
        if(auto ok=owner->accept(owner->native->BindBufferMemory(owner->native->handle,a->buffer,a->memory,bindOffset),"vkBindBufferMemory");!ok)return std::unexpected(ok.error());'''
    s=patch(s,a,b);write('src/backends/vulkan/Vulkan.cpp',s)
    for n in ('BufferPool.hpp','BufferPoolRange.hpp'):shutil.copyfile(ROOT/'experiments/vulkan_buffer_pool'/n,dst/'src/backends/vulkan'/n)
    for n in ('PoolRangeTests.cpp','PoolGpuTests.cpp'):shutil.copyfile(ROOT/'experiments/vulkan_buffer_pool'/n,dst/'tests'/n)
    c=(dst/'CMakeLists.txt').read_text(encoding='utf-8')+'''
if(GFX_BUILD_TESTS AND GFX_ENABLE_VULKAN)
  add_executable(gfx_pool_range tests/PoolRangeTests.cpp)
  target_include_directories(gfx_pool_range PRIVATE src/backends/vulkan)
  target_link_libraries(gfx_pool_range PRIVATE GraphicsSDK::Core)
  add_test(NAME pool_range_contracts COMMAND gfx_pool_range)
  add_executable(gfx_pool_gpu tests/PoolGpuTests.cpp)
  target_link_libraries(gfx_pool_gpu PRIVATE GraphicsSDK::Core)
  add_test(NAME pool_gpu COMMAND gfx_pool_gpu)
  add_test(NAME pool_gpu_validation COMMAND gfx_pool_gpu --validation)
  set_tests_properties(pool_gpu pool_gpu_validation PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
endif()
'''
    write('CMakeLists.txt',c)
    files=[audit.record_file(ROOT,p) for p in sorted(dst.rglob('*')) if p.is_file()]
    receipt=dict(schema_version=1,product_SDK_source_fingerprint=BASE,candidate_SDK_source_fingerprint=audit.sdk_fingerprint(dst),source_root=dst.relative_to(ROOT).as_posix(),files=files,patch_tool=audit.record_file(ROOT,pathlib.Path(__file__)),experimental=True,production_adopted=False)
    (dst/'candidate-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print('PASS isolated candidate',receipt['candidate_SDK_source_fingerprint'])
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);prepare(p.parse_args().output)
