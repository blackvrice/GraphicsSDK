#include <graphics/Graphics.hpp>
#include <iostream>

int main() {
    if (gfx::buildFingerprint().size() != 64) return 1;
    std::cout << "GraphicsSDK " << gfx::version << "\n";
    gfx::BindingLayout bindings{{{gfx::BindingType::ConstantBuffer,0,1,2,gfx::ShaderVisibility::All}}, {}};
    if(!gfx::validateBindingLayout(bindings))return 1;
    gfx::RenderTargetLayout renderTargets{{gfx::Format::RGBA8UNorm,gfx::Format::RGBA16Float},gfx::Format::D32Float};
    if(!gfx::validateRenderTargets(renderTargets,{true,true,gfx::CompareOp::Less}))return 1;
    if (!gfx::backendCompiled(gfx::Backend::D3D12)) return 0;
    auto device = gfx::createDevice();
    if (!device && device.error().code == gfx::ErrorCode::Unsupported) {
        std::cout << "Hardware unavailable; consumer smoke explicitly requests WARP\n";
        device=gfx::createDevice({gfx::Backend::D3D12,false,true});
    }
    if (!device) { std::cerr << device.error().message; return 1; }
    if(!(*device)->checkStatus())return 1;
    auto empty=(*device)->resourceStatistics();if(!empty||empty->trackedObjects||empty->uniqueResources||empty->resourceAllocationBytes||empty->pendingSubmissions||empty->pendingObjectReferences)return 1;
    {auto held=(*device)->createBuffer({67108864});if(!held)return 1;auto stats=(*device)->resourceStatistics();if(!stats||stats->uniqueResources!=1||stats->trackedObjects!=1||stats->resourceAllocationBytes!=67108864)return 1;}
    empty=(*device)->resourceStatistics();if(!empty||empty->uniqueResources||empty->trackedObjects||empty->resourceAllocationBytes)return 1;
    std::cout<<"Installed resource statistics: 64MiB retain/release verified\n";
    auto healthy=(*device)->recover();if(healthy||healthy.error().code!=gfx::ErrorCode::InvalidState)return 1;
    auto disabled=(*device)->removeForTesting();if(disabled||disabled.error().code!=gfx::ErrorCode::Unsupported)return 1;
    auto buffer = (*device)->createBuffer({4096});
    if (!buffer) return 1;
    if (!(*device)->caps().vertexInput || !(*device)->caps().indexedDrawing || !(*device)->caps().bindingLayouts || !(*device)->caps().depthAttachment || !(*device)->caps().multipleRenderTargets) return 1;
    gfx::VertexLayout layout{{{0,12}},{{"POSITION",0,gfx::VertexFormat::Float3,0,0}}};
    if (!gfx::validateVertexLayout(layout)) return 1;
    auto vertices=(*device)->createBuffer({36,gfx::Memory::Upload});if(!vertices)return 1;
    auto commands=(*device)->createCommandList();if(!commands)return 1;
    if(!(*commands)->begin() || !(*commands)->transition(*vertices,gfx::ResourceState::VertexBuffer) || !(*commands)->bindVertexBuffer(0,*vertices,12) || !(*commands)->end())return 1;
    auto fence=(*device)->queue().submit(**commands);if(!fence || !(*device)->queue().wait(*fence))return 1;
    auto vs=gfx::compileHlsl("float4 VS(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*2-1,.5,1);}","VS",gfx::ShaderStage::Vertex);
    auto ps=gfx::compileHlsl("cbuffer Color:register(b0,space2){float4 color;}float4 PS():SV_Target{return color;}","PS",gfx::ShaderStage::Fragment);
    if(!vs||!ps)return 1;
    gfx::GraphicsPipelineDesc pd{*vs,*ps};pd.bindingLayout=bindings;
    auto pipeline=(*device)->createGraphicsPipeline(pd);auto constants=(*device)->createBuffer({256,gfx::Memory::Upload});if(!pipeline||!constants)return 1;
    if(!(*commands)->begin() || !(*commands)->setPipeline(*pipeline) || !(*commands)->transition(*constants,gfx::ResourceState::ConstantBuffer) ||
       !(*commands)->bindResources({{},{{0,0,*constants,0,256}}}) || !(*commands)->end())return 1;
    fence=(*device)->queue().submit(**commands);if(!fence || !(*device)->queue().wait(*fence))return 1;
    ps=gfx::compileHlsl("cbuffer Color:register(b0,space2){float4 color;}struct Out{float4 a:SV_Target0;float4 b:SV_Target1;};Out PS(){Out o;o.a=color;o.b=float4(0,1,0,1);return o;}","PS",gfx::ShaderStage::Fragment);if(!ps)return 1;
    pd.fragment=*ps;pd.renderTargets=renderTargets;pd.depthState={true,true,gfx::CompareOp::Less};pipeline=(*device)->createGraphicsPipeline(pd);if(!pipeline)return 1;
    auto a=(*device)->createTexture({32,32,gfx::Format::RGBA8UNorm,true}),b=(*device)->createTexture({32,32,gfx::Format::RGBA16Float,true}),z=(*device)->createTexture({32,32,gfx::Format::D32Float,false,true});if(!a||!b||!z)return 1;
    const float rgba[]{1,0,0,1};if(!(*constants)->write(0,std::as_bytes(std::span{rgba})))return 1;
    gfx::RenderPassDesc pass{{{*a},{*b}}, {*z}};if(!gfx::validateRenderPass(pass))return 1;
    if(!(*commands)->begin() || !(*commands)->transition(*a,gfx::ResourceState::RenderTarget) || !(*commands)->transition(*b,gfx::ResourceState::RenderTarget) ||
       !(*commands)->transition(*z,gfx::ResourceState::DepthWrite) || !(*commands)->beginRenderPass(pass) || !(*commands)->setPipeline(*pipeline) ||
       !(*commands)->bindResources({{},{{0,0,*constants,0,256}}}) || !(*commands)->draw(3) || !(*commands)->endRenderPass())return 1;
    auto footprint=(*a)->footprint();auto image=(*device)->createBuffer({footprint.size,gfx::Memory::Readback});if(!image)return 1;
    if(!(*commands)->transition(*a,gfx::ResourceState::CopySource) || !(*commands)->copyTextureToBuffer(*a,*image) || !(*commands)->end())return 1;
    fence=(*device)->queue().submit(**commands);if(!fence || !(*device)->queue().wait(*fence))return 1;
    auto pixels=(*image)->read(0,footprint.size);if(!pixels)return 1;
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)for(unsigned channel=0;channel<4;++channel)
        if((*pixels)[y*footprint.rowPitch+x*4+channel]!=std::byte(channel==0||channel==3?255:0))return 1;
    std::cout<<"Installed Depth/MRT consumer: 1024 pixels verified\n";
    std::cout << (*device)->caps().adapterName << "\n";
    return 0;
}
