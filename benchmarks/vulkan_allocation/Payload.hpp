#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace allocbench {
inline constexpr unsigned width=65,height=37,bufferBytes=4096,texelBytes=4;
struct Work { bool texture{};unsigned count{}; };
inline Work work(std::string_view s){
    if(s=="buffer1")return{false,1};if(s=="buffer1000")return{false,1000};
    if(s=="texture1")return{true,1};if(s=="texture1000")return{true,1000};
    throw std::runtime_error("Unsupported resource allocation scenario");
}
inline unsigned bytes(Work w){return w.texture?width*height*texelBytes:bufferBytes;}
inline std::vector<std::byte> payload(Work w,unsigned iteration,unsigned resource){
    std::vector<std::byte> b(bytes(w));
    for(std::uint32_t i=0;i<b.size();++i){
        auto v=(i*3266489917u)^(iteration*2246822519u)^(resource*2654435761u);
        v^=v>>16;v*=2246822519u;v^=v>>13;v*=3266489917u;v^=v>>16;b[i]=std::byte(v&255);
    }
    return b;
}
inline std::vector<std::byte> pack(const std::vector<std::byte>& tight,unsigned pitch){
    if(tight.size()!=width*height*texelBytes||pitch<width*texelBytes)throw std::runtime_error("Invalid texture row layout");
    std::vector<std::byte> b(std::size_t(pitch)*height,std::byte{0xb9});
    for(unsigned y=0;y<height;++y)std::memcpy(b.data()+std::size_t(y)*pitch,tight.data()+y*width*texelBytes,width*texelBytes);
    return b;
}
inline std::vector<std::byte> unpack(const std::vector<std::byte>& padded,unsigned pitch){
    if(pitch<width*texelBytes||padded.size()<std::size_t(pitch)*height)throw std::runtime_error("Incomplete texture readback");
    std::vector<std::byte> b(width*height*texelBytes);
    for(unsigned y=0;y<height;++y)std::memcpy(b.data()+y*width*texelBytes,padded.data()+std::size_t(y)*pitch,width*texelBytes);
    return b;
}
}
