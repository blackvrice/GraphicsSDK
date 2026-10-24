#include "Model.hpp"
#include <iostream>
namespace {
unsigned checks{};
void check(bool b){++checks;churn::require(b,"CPU model contract");}
template<class F>void reject(F f){bool rejected{};try{f();}catch(const std::runtime_error&){rejected=true;}check(rejected);}
churn::Entry entry(std::uint64_t h,std::uint64_t m,std::uint64_t offset,std::uint64_t bytes){return{h,m,offset,64,64,bytes,0,1,0};}
churn::Snapshot snapshot(const churn::Model&m){churn::Snapshot s;std::set<std::uint64_t> memories;for(auto&[h,v]:m.live){(void)h;s.entries[s.count++]=v.entry;if(memories.insert(v.entry.memory).second)s.bytes+=v.entry.allocation;}s.memories=memories.size();s.buffers=m.live.size();return s;}
}
int main()try{
    using namespace churn;
    Model m(true);m.add(entry(1,10,0,64),true);m.add(entry(2,20,0,128),true);m.add(entry(3,20,64,128),true);m.verify(snapshot(m));check(m.growths==2&&m.reuses==1);
    m.remove(2);m.add(entry(4,20,0,128),true);check(m.reuses==2);m.verify(snapshot(m));
    reject([&]{auto bad=m;bad.add(entry(5,30,0,64),true);}); // Next live capacity must grow to four.
    reject([&]{auto bad=m;bad.add(entry(5,20,0,128),true);}); // All slots occupied.
    m.remove(4);reject([&]{auto bad=m;bad.add(entry(5,30,0,256),true);}); // Hole before growth.
    m.remove(3);m.remove(1);m.add(entry(5,30,0,64),true);check(m.order.size()==1);m.verify(snapshot(m));
    for(auto mutate:{0u,1u,2u,3u,4u}){
        auto s=snapshot(m);if(mutate==0)++s.bytes;if(mutate==1)++s.memories;if(mutate==2)++s.entries[0].offset;if(mutate==3)s.entries[0].typeBits=0;if(mutate==4)s.entries[0].allocation=0;
        reject([&]{m.verify(s);});
    }
    Model d(false);d.add(entry(1,10,0,64),true);reject([&]{d.add(entry(2,10,0,64),false);});
    auto host=entry(2,20,0,64);d.add(host,false);d.verify(snapshot(d));reject([&]{d.remove(99);});
    for(auto size:{1ull,63ull,64ull,65ull,127ull,128ull,129ull,255ull,256ull,257ull,1023ull,2049ull,4096ull})for(auto alignment:{1ull,64ull,256ull,4096ull}){
        Entry e{};e.required=size;e.alignment=alignment;auto s=stride(e,true);check(std::has_single_bit(s)&&s>=64&&s>=size&&s>=alignment&&s<=4096);check(!stride(e,false));e.dedicated=1;check(!stride(e,true));
    }
    for(auto alignment:{0ull,3ull,8192ull}){Entry e{};e.required=64;e.alignment=alignment;check(!stride(e,true));}
    std::cout<<"PASS churn model assertions="<<checks<<" GPU_calls=0\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
