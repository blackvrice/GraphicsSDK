#include "Adapter.hpp"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <stdexcept>

namespace bench {
Shaders compileShaders() {
    constexpr char source[] = R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 p[3] = { float2(-.8,-.8),float2(0,.8),float2(.8,-.8) };
    return float4(p[id % 3],.5,1);
}
float4 PS() : SV_Target0 { return float4(1,0,0,1); }
)";
    Shaders shaders;
    for (int stage = 0; stage < 2; ++stage) {
        Microsoft::WRL::ComPtr<ID3DBlob> output, errors;
        HRESULT status = D3DCompile(source,sizeof(source)-1,"bench.hlsl",nullptr,nullptr,stage ? "PS":"VS",stage ? "ps_5_1":"vs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&output,&errors);
        if (FAILED(status)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()):"Benchmark shader compilation failed");
        auto& binary = stage ? shaders.fragment : shaders.vertex;
        binary.resize(output->GetBufferSize()); std::memcpy(binary.data(),output->GetBufferPointer(),binary.size());
    }
    return shaders;
}
}
