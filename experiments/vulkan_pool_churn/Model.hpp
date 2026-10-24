#pragma once
#include "Snapshot.hpp"
#include <algorithm>
#include <bit>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
namespace churn {
inline void require(bool b,const char*m){if(!b)throw std::runtime_error(m);}
inline unsigned stride(const Entry&e,bool device){
    if(!device||e.dedicated||!e.required||e.required>4096||!std::has_single_bit(e.alignment)||e.alignment>4096)return 0;
    return static_cast<unsigned>(std::bit_ceil(std::max({std::uint64_t(64),e.required,e.alignment})));
}
struct Model {
    struct Value{Entry entry;bool device;};
    bool candidate{};std::map<std::uint64_t,Value> live;std::vector<std::uint64_t> order;
    std::uint64_t creates{},reuses{},growths{},dedicated{},checks{};
    explicit Model(bool c):candidate(c){}
    void add(const Entry&e,bool device){
        require(!live.contains(e.buffer)&&e.buffer&&e.memory,"Unique new native buffer");
        auto s=candidate?stride(e,device):0;
        if(!s){
            for(auto&[h,v]:live){(void)h;require(v.entry.memory!=e.memory,"Dedicated buffer cannot share memory");}
            require(e.offset==0&&e.allocation==e.required,"Exact dedicated allocation");++dedicated;
        }else{
            std::uint64_t expectedMemory{},expectedOffset{};unsigned largest{};
            for(auto memory:order){
                auto first=std::find_if(live.begin(),live.end(),[&](auto&v){return v.second.entry.memory==memory;});
                if(first==live.end())continue;auto&v=first->second;
                if(!v.device||v.entry.type!=e.type||stride(v.entry,true)!=s)continue;
                auto capacity=static_cast<unsigned>(v.entry.allocation/s);largest=std::max(largest,capacity);
                if(expectedMemory)continue;
                for(unsigned slot=0;slot<capacity;++slot){
                    auto offset=std::uint64_t(slot)*s;
                    bool used=std::any_of(live.begin(),live.end(),[&](auto&x){return x.second.entry.memory==memory&&x.second.entry.offset==offset;});
                    if(!used){expectedMemory=memory;expectedOffset=offset;break;}
                }
            }
            if(expectedMemory){require(e.memory==expectedMemory&&e.offset==expectedOffset,"First free slot reused before growth");++reuses;}
            else{
                for(auto&[h,v]:live){(void)h;require(v.entry.memory!=e.memory,"Growth uses a new native block");}
                auto capacity=largest?std::min(largest*2,65536u/s):1u;
                require(e.offset==0&&e.allocation==std::uint64_t(s)*capacity,"Adaptive live-block growth policy");order.push_back(e.memory);++growths;
            }
        }
        live.emplace(e.buffer,Value{e,device});++creates;
    }
    void remove(std::uint64_t h){require(live.erase(h)==1,"Exactly one lease removed");std::erase_if(order,[&](auto memory){return std::none_of(live.begin(),live.end(),[&](auto&x){return x.second.entry.memory==memory;});});}
    void verify(const Snapshot&s){
        ++checks;require(s.count==live.size()&&s.buffers==live.size()&&!s.images,"Exact live buffer set");std::set<std::uint64_t> seen,memories;std::uint64_t bytes{};
        for(unsigned i=0;i<s.count;++i){auto&e=s.entries[i];auto v=live.find(e.buffer);require(v!=live.end()&&v->second.entry==e&&seen.insert(e.buffer).second,"Stable native owner metadata");
            require(e.alignment&&e.offset%e.alignment==0&&e.offset<e.allocation&&e.required<=e.allocation-e.offset&&e.type<32&&(e.typeBits&(1u<<e.type)),"Native binding range/type/alignment");
            if(memories.insert(e.memory).second)bytes+=e.allocation;
            for(unsigned j=0;j<i;++j){auto&b=s.entries[j];if(e.memory==b.memory){
                require(e.type==b.type&&e.allocation==b.allocation,"Shared native memory identity");
                require(e.offset>=b.offset+b.required||b.offset>=e.offset+e.required,"Distinct live ranges never overlap");
            }}
        }
        require(s.memories==memories.size()&&s.bytes==bytes,"No unbound/empty native memory cache");
    }
};
}
