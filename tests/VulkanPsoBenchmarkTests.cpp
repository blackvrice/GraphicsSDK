#include "../benchmarks/vulkan_pso/Frame.hpp"
#include <algorithm>
#include <iostream>

int main()try{
    using namespace psobench;unsigned checks{};auto check=[&](bool v){++checks;if(!v)throw std::runtime_error("Pipeline image reference contract");};
    check(work("pso1")==1&&work("pso8")==8);
    bool bad{};try{(void)work("pso0");}catch(const std::runtime_error&){bad=true;}check(bad);
    bad=false;try{(void)reference(0,0);}catch(const std::runtime_error&){bad=true;}check(bad);
    const std::array<unsigned,9> boundaries{5,11,18,25,32,39,46,53,60};
    for(unsigned i=0;i<9;++i)check(edge(i,8)==boundaries[i]);
    auto pixel=[](const auto& b,unsigned x,unsigned y){std::array<unsigned,4> p;for(unsigned k=0;k<4;++k)p[k]=std::to_integer<unsigned>(b[(y*width+x)*4+k]);return p;};
    const std::array<unsigned,4> red{255,0,0,255},green{0,255,0,255},blue{0,0,255,255};
    for(auto count:{1u,8u}){
        auto a=reference(0,count),b=reference(1,count);check(a.size()==9620&&a.size()==b.size());
        for(unsigned i=0;i<count;++i){check(pixel(a,edge(i,count),top)==(i%2?green:red));check(pixel(a,edge(i+1,count)-1,bottom-1)==(i%2?green:red));}
        check(pixel(a,4,3)==blue&&pixel(a,60,33)==blue&&pixel(a,5,34)==blue);
        unsigned clear{},colored{},changed{};for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){auto p=pixel(a,x,y);clear+=p==blue;colored+=p==red||p==green;changed+=p!=pixel(b,x,y);}
        check(clear==700&&colored==1705&&changed==2405);check(reference(2,count)==a);
    }
    std::cout<<"PASS pso_reference_assertions="<<checks<<" stripes=1,8 covered_pixels=1705 clear_pixels=700 GPU_calls=0\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
