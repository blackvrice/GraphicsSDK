"""Rebase the reviewed adaptive slab onto the current ownership-hardened product."""
import argparse
import difflib
import json
import pathlib
import shutil
import audit_licenses as audit
ROOT=pathlib.Path(__file__).resolve().parents[1]
BASE='2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401'
PRIOR='3b0f54c6caf225a0d9ed3d43f00543e3f29579d5f72e00e2c481aeadbeb94808'
def replace(s,a,b):
    if s.count(a)!=1:raise ValueError('Unique rebase anchor missing: '+a[:70])
    return s.replace(a,b)
def prepare(dst):
    dst=dst.resolve();old=ROOT/'out/vulkan-buffer-pool-fault-source'
    if not dst.is_relative_to(ROOT/'out') or dst.exists():raise ValueError('Exclusive out/ source root required')
    if audit.sdk_fingerprint(ROOT)!=BASE or audit.sdk_fingerprint(old)!=PRIOR:raise ValueError('Product/prior candidate changed')
    prior=json.loads((old/'candidate-receipt.json').read_text(encoding='utf-8'))
    for r in prior['files']:
        if audit.record_file(ROOT,ROOT/r['path'])!=r:raise ValueError('Changed historical candidate input')
    dst.mkdir(parents=True)
    for folder in ('include','src','cmake','tests','examples','tools','shaders'):shutil.copytree(ROOT/folder,dst/folder,ignore=shutil.ignore_patterns('__pycache__'))
    for name in ('CMakeLists.txt','LICENSE'):shutil.copyfile(ROOT/name,dst/name)
    def write(p,s):p.write_bytes(s.replace('\r\n','\n').replace('\n','\r\n').encode())
    p=dst/'src/backends/vulkan/Vulkan.cpp';before=s=p.read_text(encoding='utf-8')
    s=replace(s,'#include "Creation.hpp"','#include "Creation.hpp"\n#include "BufferPoolRange.hpp"\n#include <mutex>\n#include <set>')
    s=replace(s,'X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements)','X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements2) X(GetBufferMemoryRequirements)')
    s=replace(s,'struct NativeSurface {','#include "BufferPool.hpp"\nstruct NativeSurface {')
    s=replace(s,'    VkDeviceMemory memory{};\n    VkDeviceSize bytes{};','    VkDeviceMemory memory{};\n    std::shared_ptr<BufferBlock> block;\n    std::uint32_t slot{};\n    VkDeviceSize bytes{};')
    s=replace(s,'        if (memory) native->FreeMemory(native->handle, memory, nullptr);','        if (block) { block->give(slot); block.reset(); }\n        else if (memory) native->FreeMemory(native->handle, memory, nullptr);')
    s=replace(s,'    std::vector<std::weak_ptr<Allocation>> allocations;','    std::vector<std::weak_ptr<Allocation>> allocations;\n    std::vector<std::weak_ptr<BufferBlock>> bufferBlocks;')
    s=replace(s,'        for (auto& w : owner->allocations) if (auto a = w.lock()) { ++result.uniqueResources; result.resourceAllocationBytes += a->bytes; }','''        std::set<const BufferBlock*> counted;
        for (auto& w : owner->allocations) if (auto a = w.lock()) {
            ++result.uniqueResources;
            if (a->block) { if(counted.insert(a->block.get()).second)result.resourceAllocationBytes+=a->block->slots.bytes(); }
            else result.resourceAllocationBytes+=a->bytes;
        }''')
    s=replace(s,'        VkMemoryRequirements req{}; owner->native->GetBufferMemoryRequirements(owner->native->handle, a->buffer, &req);','''        VkMemoryDedicatedRequirements dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
        VkMemoryRequirements2 requirements{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};requirements.pNext=&dedicated;
        VkBufferMemoryRequirementsInfo2 query{VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2};query.buffer=a->buffer;
        owner->native->GetBufferMemoryRequirements2(owner->native->handle,&query,&requirements);
        const auto req=requirements.memoryRequirements;''')
    a='''        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocate.allocationSize = req.size; allocate.memoryTypeIndex = *type;
        if (auto ok = owner->accept(vulkan::acquireOutput(a->memory, owner->native->AllocateMemory, owner->native->handle, &allocate, nullptr), "vkAllocateMemory"); !ok) return std::unexpected(ok.error());
        if (auto ok = owner->accept(owner->native->BindBufferMemory(owner->native->handle, a->buffer, a->memory, 0), "vkBindBufferMemory"); !ok) return std::unexpected(ok.error());'''
    historical=(old/'src/backends/vulkan/Vulkan.cpp').read_text(encoding='utf-8')
    start=historical.index('        VkDeviceSize bindOffset{};');end=historical.index('        auto value = std::make_shared<VulkanBuffer>();',start)
    b=historical[start:end].rstrip('\n')
    b=replace(b,'auto r=owner->native->AllocateMemory(owner->native->handle,&pool,nullptr,&block->memory);','auto r=vulkan::acquireOutput(block->memory,owner->native->AllocateMemory,owner->native->handle,&pool,nullptr);')
    b=replace(b,'else {block->memory={};if(r!=','else {if(r!=')
    b=replace(b,'''            // Failed Vulkan output handles have undefined contents. Acquire ownership only on success.
            VkDeviceMemory allocated{};
            if(auto ok=owner->accept(owner->native->AllocateMemory(owner->native->handle,&allocate,nullptr,&allocated),"vkAllocateMemory");!ok)return std::unexpected(ok.error());
            a->memory=allocated;''','''            if(auto ok=owner->accept(vulkan::acquireOutput(a->memory,owner->native->AllocateMemory,owner->native->handle,&allocate,nullptr),"vkAllocateMemory");!ok)return std::unexpected(ok.error());''')
    s=replace(s,a,b);write(p,s)
    for name in ('BufferPool.hpp','BufferPoolRange.hpp'):shutil.copyfile(old/'src/backends/vulkan'/name,dst/'src/backends/vulkan'/name)
    # Pool policy and public ABI are exact historical bytes. All current ownership
    # guards remain, with one extra guard for native block allocation.
    assert s.count('vulkan::acquireOutput(')==22
    assert (dst/'src/backends/vulkan/Presentation.hpp').read_bytes()==(ROOT/'src/backends/vulkan/Presentation.hpp').read_bytes()
    for p in (ROOT/'include').rglob('*.hpp'):assert p.read_bytes()==(dst/p.relative_to(ROOT)).read_bytes()
    patch=''.join(difflib.unified_diff(before.splitlines(True),s.splitlines(True),fromfile='a/src/backends/vulkan/Vulkan.cpp',tofile='b/src/backends/vulkan/Vulkan.cpp'))
    for name in ('BufferPool.hpp','BufferPoolRange.hpp'):
        patch+=''.join(difflib.unified_diff([], (dst/'src/backends/vulkan'/name).read_text(encoding='utf-8').splitlines(True),fromfile='/dev/null',tofile='b/src/backends/vulkan/'+name))
    (ROOT/'experiments/vulkan_pool_churn/candidate.patch').write_bytes(patch.encode())
    files=[audit.record_file(ROOT,p) for p in sorted(dst.rglob('*')) if p.is_file()]
    receipt=dict(schema_version=1,product_SDK_source_fingerprint=BASE,prior_candidate_SDK_source_fingerprint=PRIOR,candidate_SDK_source_fingerprint=audit.sdk_fingerprint(dst),files=files,patch_tool=audit.record_file(ROOT,pathlib.Path(__file__)),production_adopted=False)
    (dst/'candidate-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PASS rebased candidate',receipt['candidate_SDK_source_fingerprint'])
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);prepare(p.parse_args().output)
