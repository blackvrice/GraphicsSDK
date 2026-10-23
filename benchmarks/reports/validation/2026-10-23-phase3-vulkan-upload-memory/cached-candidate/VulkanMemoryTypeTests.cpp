#include "MemoryType.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() try {
    using gfx::Memory; using gfx::vulkan::BufferMemoryType; using gfx::vulkan::selectBufferMemoryType;
    unsigned checks{};
    auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("Buffer memory preference contract");};
    // Native property shapes observed on this NVIDIA and Intel, independent of handles.
    const std::array nvidia{BufferMemoryType{},BufferMemoryType{true},BufferMemoryType{true},
        BufferMemoryType{false,true,true,false},BufferMemoryType{false,true,true,true},BufferMemoryType{true,true,true,false}};
    const std::array intel{BufferMemoryType{true},BufferMemoryType{true,true,true,false},BufferMemoryType{true,true,true,true}};
    check(selectBufferMemoryType(nvidia,~0u,Memory::Upload)==4);check(selectBufferMemoryType(intel,~0u,Memory::Upload)==2);
    check(selectBufferMemoryType(nvidia,~0u,Memory::Readback)==3);check(selectBufferMemoryType(intel,~0u,Memory::Readback)==1);
    check(selectBufferMemoryType(nvidia,~0u,Memory::Device)==1);check(selectBufferMemoryType(intel,~0u,Memory::Device)==0);
    check(selectBufferMemoryType(nvidia,~(1u<<4),Memory::Upload)==3);check(selectBufferMemoryType(intel,1u<<1,Memory::Upload)==1);
    check(!selectBufferMemoryType(nvidia,0,Memory::Upload));check(!selectBufferMemoryType({},~0u,Memory::Upload));
    check(!selectBufferMemoryType(nvidia,~0u,static_cast<Memory>(99)));
    const std::array fallback{BufferMemoryType{false,true,false,false},BufferMemoryType{false,true,false,true},BufferMemoryType{false,true,true,false},BufferMemoryType{false,true,true,true}};
    for(unsigned mask=0;mask<16;++mask){
        std::optional<unsigned> expected;
        for(unsigned i=0;i<4;++i)if(mask&(1u<<i))expected=i;
        check(selectBufferMemoryType(fallback,mask,Memory::Upload)==expected);
    }
    for(unsigned i=0;i<nvidia.size();++i)for(auto memory:{Memory::Device,Memory::Upload,Memory::Readback}) {
        auto r=selectBufferMemoryType(nvidia,1u<<i,memory);
        check(bool(r)==(memory==Memory::Device?nvidia[i].deviceLocal:nvidia[i].hostVisible));if(r)check(*r==i);
    }
    std::vector<BufferMemoryType> limit(33);limit[31]={false,true,true,true};limit[32]={false,true,true,true};
    check(selectBufferMemoryType(limit,1u<<31,Memory::Upload)==31);limit[31]={};check(!selectBufferMemoryType(limit,~0u,Memory::Upload));
    auto ties=fallback;ties[0]=ties[3];check(selectBufferMemoryType(ties,~0u,Memory::Upload)==0);
    std::cout<<"PASS memory_type_assertions="<<checks<<" GPU_calls=0 Upload_cached_preference=true Readback_policy_changed=false\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
