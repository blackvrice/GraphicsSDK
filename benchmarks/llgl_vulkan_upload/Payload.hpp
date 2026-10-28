#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace llglupload {
struct Shape{unsigned w{},h{},bpp{};};
inline Shape shape(std::string_view s){if(s=="odd")return{65,37,4};if(s=="upload1080")return{1920,1080,4};if(s=="upload4k")return{3840,2160,4};if(s=="upload4k16f")return{3840,2160,8};throw std::runtime_error("Unsupported scenario");}
// Same body as the frozen SDK/SDL upload payload; tooling checks this equality.
inline void payload(std::vector<std::byte>& data,Shape s,unsigned seed){
    for(std::size_t i=0;i<std::size_t(s.w)*s.h;++i){auto v=std::uint32_t(i)*1315423911u+seed*2654435761u;v^=v>>16;
        if(s.bpp==4)std::memcpy(data.data()+i*4,&v,4);else{constexpr std::uint16_t halves[]{0,0x3400,0x3800,0x3c00,0xb400,0x4000,0x4200,0x0400};for(unsigned k=0;k<4;++k){auto h=halves[(v>>(k*3))&7];std::memcpy(data.data()+i*8+k*2,&h,2);}}
    }
}
}
