#include "BufferPoolRange.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>
int main()try{
    using gfx::vulkan::BufferSlots;unsigned checks{};auto check=[&](bool v){++checks;if(!v)throw std::runtime_error("Buffer slab contract");};
    for(auto s:{0ull,4097ull,UINT64_MAX})check(BufferSlots::stride(s,64)==0);
    for(auto a:{0ull,3ull,8192ull,UINT64_MAX})check(BufferSlots::stride(64,a)==0);
    check(BufferSlots::stride(64,64,true)==0);check(BufferSlots::stride(65,64)==128);check(BufferSlots::stride(1,256)==256);
    for(auto invalid:{0u,3u,32u,8192u}){BufferSlots s(invalid);check(s.capacity()==0&&!s.take()&&!s.release(0));}
    for(unsigned stride=64;stride<=4096;stride*=2){
        BufferSlots slots(stride);std::set<unsigned> offsets;std::vector<unsigned> ids;
        for(unsigned i=0;i<slots.capacity();++i){auto p=slots.take();check(p.has_value());ids.push_back(*p);offsets.insert(*p*stride);}
        check(offsets.size()==slots.capacity());check(!slots.take());check(!slots.release(slots.capacity()));
        for(unsigned i=0;i<ids.size();i+=2)check(slots.release(ids[i]));
        for(unsigned i=0;i<ids.size();i+=2){auto p=slots.take();check(p&&*p==ids[i]);}
        for(auto id:ids)check(slots.release(id));check(!slots.release(ids[0]));
        for(std::uint64_t size=1;size<=4096;++size){auto n=BufferSlots::stride(size,stride);check(n>=size&&n>=stride&&(n%stride)==0&&n<=4096);}
    }
    std::cout<<"PASS pool_range_assertions="<<checks<<" GPU_calls=0\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
