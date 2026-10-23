#pragma once
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace psobench {
inline constexpr unsigned width=65,height=37,left=5,top=3,right=60,bottom=34;
inline unsigned work(std::string_view name) {
    if(name=="pso1")return 1;
    if(name=="pso8")return 8;
    throw std::runtime_error("Unsupported pipeline scenario");
}
inline unsigned edge(unsigned i,unsigned count){return left+(right-left)*i/count;}
inline std::vector<std::byte> reference(unsigned iteration,unsigned count) {
    if(count!=1&&count!=8)throw std::runtime_error("Unsupported pipeline count");
    std::vector<std::byte> bytes(width*height*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        std::array<unsigned,4> color{0,iteration%2?255u:0u,255,255};
        if(x>=left&&x<right&&y>=top&&y<bottom) {
            unsigned stripe{};
            while(stripe+1<count&&x>=edge(stripe+1,count))++stripe;
            color=(iteration+stripe)%2?std::array<unsigned,4>{0,255,0,255}:std::array<unsigned,4>{255,0,0,255};
        }
        for(unsigned k=0;k<4;++k)bytes[(y*width+x)*4+k]=std::byte(color[k]);
    }
    return bytes;
}
}
