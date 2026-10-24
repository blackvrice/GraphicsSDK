#include "../benchmarks/vulkan_allocation_trace/Accounting.hpp"
#include <iostream>
int main()try{
    using namespace trace;unsigned checks{};auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("Allocation trace ledger contract");};
    Ledger l;Counts c;check(l.live()==Live{});
    l.event(c,AllocateMemory,false,1,4096);check(c.calls[AllocateMemory]==1&&c.successes[AllocateMemory]==0&&l.live()==Live{});
    l.event(c,AllocateMemory,true,1,4096);l.event(c,AllocateMemory,true,2,8192);check(l.live()==Live{2,12288,0,0});
    l.event(c,CreateBuffer,true,1);l.event(c,CreateImage,true,1);check(l.live()==Live{2,12288,1,1});
    l.event(c,BindBufferMemory,true);l.event(c,BindImageMemory,true);check(c.successes[BindBufferMemory]==1&&c.successes[BindImageMemory]==1);
    l.event(c,FreeMemory,true,1);check(l.live()==Live{1,8192,1,1}&&c.freedBytes==4096);
    l.event(c,AllocateMemory,true,1,2048);check(l.live()==Live{2,10240,1,1});
    l.event(c,FreeMemory,true,2);l.event(c,FreeMemory,true,1);l.event(c,DestroyBuffer,true,1);l.event(c,DestroyImage,true,1);
    check(l.live()==Live{});check(c.allocatedBytes==c.freedBytes&&c.errors==0);
    l.event(c,FreeMemory,true,0);l.event(c,DestroyBuffer,true,0);l.event(c,DestroyImage,true,0);check(c.errors==0&&l.live()==Live{});
    l.event(c,FreeMemory,true,99);check(c.errors==1);l.event(c,DestroyBuffer,true,99);check(c.errors==2);l.event(c,DestroyImage,true,99);check(c.errors==3);
    l.event(c,CreateBuffer,true,2);l.event(c,CreateBuffer,true,2);check(c.errors==4&&l.live().buffers==1);
    l.event(c,CreateImage,true,0);check(c.errors==5);l.event(c,AllocateMemory,true,0,1);check(c.errors==6);
    Ledger other;Counts oc;other.event(oc,AllocateMemory,true,1,32);check(l.live().memories==0&&other.live().bytes==32);
    std::cout<<"PASS trace_ledger_assertions="<<checks<<" GPU_calls=0\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
