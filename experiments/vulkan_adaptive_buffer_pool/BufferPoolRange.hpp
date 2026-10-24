#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <optional>

namespace gfx::vulkan {
// Experimental growing size-class slab. No host mappings or image allocations share it.
class BufferSlots {
    std::array<std::uint64_t,16> used{};
    std::uint32_t stride_{}, capacity_{};
public:
    static constexpr std::uint32_t blockBytes=65536;
    static std::uint32_t stride(std::uint64_t size,std::uint64_t alignment,bool dedicated=false){
        if(dedicated||!size||size>4096||!std::has_single_bit(alignment)||alignment>4096)return 0;
        auto n=std::uint64_t(64);while(n<size||n<alignment)n*=2;return static_cast<std::uint32_t>(n);
    }
    static std::uint32_t nextCapacity(std::uint32_t stride,std::uint32_t largest){
        if(!std::has_single_bit(stride)||stride<64||stride>4096)return 0;
        const auto limit=blockBytes/stride;
        if(largest&&(!std::has_single_bit(largest)||largest>limit))return 0;
        return largest?std::min(largest*2,limit):1;
    }
    explicit BufferSlots(std::uint32_t s,std::uint32_t capacity):
        stride_(std::has_single_bit(s)&&s>=64&&s<=4096?s:0){
        if(stride_&&std::has_single_bit(capacity)&&capacity<=blockBytes/stride_)capacity_=capacity;
        else stride_=0;
    }
    std::uint32_t stride()const{return stride_;}
    std::uint32_t capacity()const{return capacity_;}
    std::uint32_t bytes()const{return stride_*capacity_;}
    std::optional<std::uint32_t> take(){
        for(std::uint32_t i=0;i<capacity();++i)if(!(used[i/64]&(std::uint64_t(1)<<(i%64)))){used[i/64]|=std::uint64_t(1)<<(i%64);return i;}
        return {};
    }
    bool release(std::uint32_t slot){
        if(slot>=capacity())return false;auto bit=std::uint64_t(1)<<(slot%64);if(!(used[slot/64]&bit))return false;used[slot/64]&=~bit;return true;
    }
};
}
