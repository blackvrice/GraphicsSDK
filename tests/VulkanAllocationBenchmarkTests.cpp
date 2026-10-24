#include "../benchmarks/vulkan_allocation/Payload.hpp"
#include <algorithm>
#include <iostream>
#include <set>

int main()try{
    using namespace allocbench;unsigned checks{};auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("Allocation payload contract");};
    check(!work("buffer1").texture&&work("buffer1000").count==1000);check(work("texture1").texture&&work("texture1000").count==1000);
    bool bad{};try{(void)work("buffer0");}catch(const std::runtime_error&){bad=true;}check(bad);
    for(auto scenario:{"buffer1000","texture1000"}){
        auto w=work(scenario);auto a=payload(w,0,0);check(a.size()==(w.texture?9620:4096));check(a==payload(w,0,0));check(a!=payload(w,1,0));
        std::set<std::vector<std::byte>> prefixes;
        for(unsigned i=0;i<1000;++i){auto p=payload(w,7,i);prefixes.emplace(p.begin(),p.begin()+16);}
        check(prefixes.size()==1000);
    }
    auto a=payload(work("texture1"),3,9),padded=pack(a,512);check(unpack(padded,512)==a);check(unpack(pack(a,260),260)==a);
    for(unsigned y=0;y<height;++y)check(std::all_of(padded.begin()+y*512+260,padded.begin()+(y+1)*512,[](auto b){return b==std::byte{0xb9};}));
    bad=false;try{(void)pack(a,259);}catch(const std::runtime_error&){bad=true;}check(bad);
    bad=false;try{(void)unpack({},512);}catch(const std::runtime_error&){bad=true;}check(bad);
    check(payload(work("buffer1"),0,0)[0]==std::byte{0});
    std::cout<<"PASS allocation_reference_assertions="<<checks<<" distinct_resource_prefixes=1000 padded_rows=37 GPU_calls=0\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
