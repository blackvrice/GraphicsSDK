#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace drawbench {
inline constexpr unsigned width=65, height=37, left=5, top=3, right=60, bottom=34;
struct Work { unsigned calls, instances; };
inline Work work(std::string_view scenario) {
    if(scenario=="draw1")return{1,1};
    if(scenario=="draw1000")return{1000,1};
    if(scenario=="instanced1000")return{1,1000};
    throw std::runtime_error("Unsupported draw scenario");
}
inline std::vector<std::byte> reference(unsigned iteration) {
    std::vector<std::byte> bytes(width*height*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        const bool covered=x>=left&&x<right&&y>=top&&y<bottom;
        const std::array<unsigned,4> color=covered?(iteration%2?std::array<unsigned,4>{0,255,0,255}:std::array<unsigned,4>{255,0,0,255}):
            (iteration%2?std::array<unsigned,4>{0,255,255,255}:std::array<unsigned,4>{0,0,255,255});
        for(unsigned c=0;c<4;++c)bytes[(y*width+x)*4+c]=std::byte(color[c]);
    }
    return bytes;
}
}
