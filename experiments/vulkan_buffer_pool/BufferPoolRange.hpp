#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <optional>

namespace gfx::vulkan {
// Experimental size-class slab. No host mappings or image allocations share it.
class BufferSlots {
    std::array<std::uint64_t,16> used{};
    std::uint32_t stride_{};
public:
    static constexpr std::uint32_t blockBytes=65536;
    static std::uint32_t stride(std::uint64_t size,std::uint64_t alignment,bool dedicated=false){
        if(dedicated||!size||size>4096||!std::has_single_bit(alignment)||alignment>4096)return 0;
        auto n=std::uint64_t(64);while(n<size||n<alignment)n*=2;return static_cast<std::uint32_t>(n);
    }
    explicit BufferSlots(std::uint32_t s):stride_(std::has_single_bit(s)&&s>=64&&s<=4096?s:0){}
    std::uint32_t stride()const{return stride_;}
    std::uint32_t capacity()const{return stride_?blockBytes/stride_:0;}
    std::optional<std::uint32_t> take(){
        for(std::uint32_t i=0;i<capacity();++i)if(!(used[i/64]&(std::uint64_t(1)<<(i%64)))){used[i/64]|=std::uint64_t(1)<<(i%64);return i;}
        return {};
    }
    bool release(std::uint32_t slot){
        if(slot>=capacity())return false;auto bit=std::uint64_t(1)<<(slot%64);if(!(used[slot/64]&bit))return false;used[slot/64]&=~bit;return true;
    }
};
}
