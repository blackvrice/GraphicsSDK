#include <graphics/Graphics.hpp>
#include <iostream>

int main() {
    if (gfx::buildFingerprint().size() != 64) return 1;
    std::cout << "GraphicsSDK " << gfx::version << "\n";
    gfx::BindingLayout bindings{{{gfx::BindingType::ConstantBuffer,0,1,2,gfx::ShaderVisibility::All}}, {}};
    if(!gfx::validateBindingLayout(bindings))return 1;
    if (!gfx::backendCompiled(gfx::Backend::D3D12)) return 0;
    auto device = gfx::createDevice();
    if (!device && device.error().code == gfx::ErrorCode::Unsupported) {
        std::cout << "Hardware unavailable; consumer smoke explicitly requests WARP\n";
        device=gfx::createDevice({gfx::Backend::D3D12,false,true});
    }
    if (!device) { std::cerr << device.error().message; return 1; }
    auto buffer = (*device)->createBuffer({4096});
    if (!buffer) return 1;
    if (!(*device)->caps().vertexInput || !(*device)->caps().indexedDrawing || !(*device)->caps().bindingLayouts) return 1;
    gfx::VertexLayout layout{{{0,12}},{{"POSITION",0,gfx::VertexFormat::Float3,0,0}}};
    if (!gfx::validateVertexLayout(layout)) return 1;
    auto vertices=(*device)->createBuffer({36,gfx::Memory::Upload});if(!vertices)return 1;
    auto commands=(*device)->createCommandList();if(!commands)return 1;
    if(!(*commands)->begin() || !(*commands)->transition(*vertices,gfx::ResourceState::VertexBuffer) || !(*commands)->bindVertexBuffer(0,*vertices,12) || !(*commands)->end())return 1;
    auto fence=(*device)->queue().submit(**commands);if(!fence || !(*device)->queue().wait(*fence))return 1;
    auto vs=gfx::compileHlsl("float4 VS(uint id:SV_VertexID):SV_Position{return float4(id,0,0,1);}","VS",gfx::ShaderStage::Vertex);
    auto ps=gfx::compileHlsl("cbuffer Color:register(b0,space2){float4 color;}float4 PS():SV_Target{return color;}","PS",gfx::ShaderStage::Fragment);
    if(!vs||!ps)return 1;
    gfx::GraphicsPipelineDesc pd{*vs,*ps};pd.bindingLayout=bindings;
    auto pipeline=(*device)->createGraphicsPipeline(pd);auto constants=(*device)->createBuffer({256,gfx::Memory::Upload});if(!pipeline||!constants)return 1;
    if(!(*commands)->begin() || !(*commands)->setPipeline(*pipeline) || !(*commands)->transition(*constants,gfx::ResourceState::ConstantBuffer) ||
       !(*commands)->bindResources({{},{{0,0,*constants,0,256}}}) || !(*commands)->end())return 1;
    fence=(*device)->queue().submit(**commands);if(!fence || !(*device)->queue().wait(*fence))return 1;
    std::cout << (*device)->caps().adapterName << "\n";
    return 0;
}
