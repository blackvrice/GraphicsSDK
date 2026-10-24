#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>

namespace trace {
enum Call { CreateBuffer,DestroyBuffer,CreateImage,DestroyImage,AllocateMemory,FreeMemory,BindBufferMemory,BindImageMemory,CallCount };
inline constexpr const char* names[]={"CreateBuffer","DestroyBuffer","CreateImage","DestroyImage","AllocateMemory","FreeMemory","BindBufferMemory","BindImageMemory"};
struct Counts {
    std::array<std::uint64_t,CallCount> calls{},successes{};
    std::uint64_t allocatedBytes{},freedBytes{},errors{};
};
struct Live {
    std::uint64_t memories{},bytes{},buffers{},images{};
    bool operator==(const Live&)const=default;
};
struct Ledger {
    std::map<std::uint64_t,std::uint64_t> memories,buffers,images;
    Live live()const{Live l{memories.size(),0,buffers.size(),images.size()};for(auto& [key,size]:memories){(void)key;l.bytes+=size;}return l;}
    void event(Counts& c,Call call,bool success,std::uint64_t handle=0,std::uint64_t size=0){
        ++c.calls[call];if(!success)return;
        if(!handle&&(call==DestroyBuffer||call==DestroyImage||call==FreeMemory))return;
        ++c.successes[call];
        auto insert=[&](auto& m){if(!handle||m.size()>=100000||!m.emplace(handle,size).second)++c.errors;};
        auto erase=[&](auto& m){if(handle&&!m.erase(handle))++c.errors;};
        switch(call){
        case CreateBuffer:insert(buffers);break;case DestroyBuffer:erase(buffers);break;
        case CreateImage:insert(images);break;case DestroyImage:erase(images);break;
        case AllocateMemory:insert(memories);c.allocatedBytes+=size;break;
        case FreeMemory:
            if(handle){auto it=memories.find(handle);if(it==memories.end())++c.errors;else{c.freedBytes+=it->second;memories.erase(it);}}break;
        default:break;
        }
    }
};
}
