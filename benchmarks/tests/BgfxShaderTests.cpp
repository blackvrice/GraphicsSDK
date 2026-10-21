#include "common/BgfxShader.hpp"
#include <iostream>
#include <functional>

int main() try {
    unsigned checks=0;
    auto require=[&](bool ok) { if (!ok) throw std::runtime_error("bgfx shader contract failed"); ++checks; };
    auto rejects=[&](const std::function<void()>& action) {
        bool failed=false; try { action(); } catch (const std::runtime_error&) { failed=true; }
        require(failed);
    };
    auto shaders=bench::compileShaders();
    for (bool fragment : {false,true}) {
        const auto& dxbc=fragment?shaders.fragment:shaders.vertex;
        auto wrapped=bench::bgfxShaderContainer(dxbc,fragment);
        require(wrapped.size()==dxbc.size()+30 && wrapped[0]==static_cast<std::byte>(fragment?'F':'V') && wrapped[3]==std::byte{13}
            && std::equal(dxbc.begin(),dxbc.end(),wrapped.begin()+26));
    }
    rejects([&] { bench::bgfxShaderContainer(shaders.vertex,true); });
    rejects([&] { bench::bgfxShaderContainer({},false); });
    const std::byte invalid[]{std::byte{'D'},std::byte{'X'},std::byte{'B'},std::byte{'C'}};
    rejects([&] { bench::bgfxShaderContainer(invalid,false); });
    auto compile=[](const char* source,const char* profile) {
        Microsoft::WRL::ComPtr<ID3DBlob> binary,error;
        if (FAILED(D3DCompile(source,std::strlen(source),"contract.hlsl",nullptr,nullptr,"main",profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binary,&error)))
            throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"contract shader compilation failed");
        const auto bytes=static_cast<const std::byte*>(binary->GetBufferPointer());
        return std::vector<std::byte>(bytes,bytes+binary->GetBufferSize());
    };
    auto bound=compile("Texture2D<float4> tex:register(t0); float4 main():SV_Target0{return tex.Load(int3(0,0,0));}","ps_5_1");
    rejects([&] { bench::bgfxShaderContainer(bound,true); });
    auto attributes=compile("float4 main(float4 p:POSITION):SV_Position{return p;}","vs_5_1");
    rejects([&] { bench::bgfxShaderContainer(attributes,false); });
    auto varyings=compile("struct O{float4 p:SV_Position;float4 c:COLOR;}; O main(uint id:SV_VertexID){O o;o.p=float4(id,0,0,1);o.c=1;return o;}","vs_5_1");
    rejects([&] { bench::bgfxShaderContainer(varyings,false); });
    std::cout<<checks<<" bgfx shader contract checks passed\n";
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
