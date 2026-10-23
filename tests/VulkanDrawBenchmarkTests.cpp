#include "../benchmarks/vulkan_draw/Frame.hpp"
#include <algorithm>
#include <iostream>

int main() try {
    using namespace drawbench;
    unsigned checks{};auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("Draw reference contract");};
    auto a=work("draw1"),b=work("draw1000"),c=work("instanced1000");
    check(a.calls==1&&a.instances==1);check(b.calls==1000&&b.instances==1);check(c.calls==1&&c.instances==1000);
    check(b.calls*b.instances==c.calls*c.instances);
    bool rejected{};try{(void)work("unknown");}catch(const std::runtime_error&){rejected=true;}check(rejected);
    const auto even=reference(0),odd=reference(1);check(even.size()==9620&&odd.size()==even.size());
    auto pixel=[&](const auto& bytes,unsigned x,unsigned y){std::array<std::byte,4> p;std::copy_n(bytes.begin()+(y*width+x)*4,4,p.begin());return p;};
    const std::array red{std::byte{255},std::byte{0},std::byte{0},std::byte{255}};
    const std::array green{std::byte{0},std::byte{255},std::byte{0},std::byte{255}};
    const std::array blue{std::byte{0},std::byte{0},std::byte{255},std::byte{255}};
    const std::array cyan{std::byte{0},std::byte{255},std::byte{255},std::byte{255}};
    check(pixel(even,5,3)==red&&pixel(even,59,33)==red);
    check(pixel(even,4,3)==blue&&pixel(even,60,33)==blue&&pixel(even,5,34)==blue);
    check(pixel(odd,5,3)==green&&pixel(odd,64,36)==cyan);
    unsigned covered{},cleared{},changed{};
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){auto p=pixel(even,x,y);covered+=p==red;cleared+=p==blue;changed+=p!=pixel(odd,x,y);}
    check(covered==1705&&cleared==700);check(changed==2405);check(reference(2)==even);
    std::cout<<"PASS draw_reference_assertions="<<checks<<" covered_pixels="<<covered<<" clear_pixels="<<cleared<<" GPU_calls=0\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
