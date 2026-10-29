#pragma once
#include "../llgl_vulkan_pso/Support.hpp"
#include "../vulkan_draw/Frame.hpp"
#include "../../tests/fixtures/SpirvFixtures.hpp"
#include "../common/Adapter.hpp"
namespace llglupload {
struct Draw {
    Context& c;LLGL::Texture* texture{};const LLGL::RenderPass* pass{};LLGL::RenderTarget* target{};LLGL::Shader *vs{},*rs{},*gs{};LLGL::PipelineState *red{},*green{};LLGL::Buffer* readback{};LLGL::CommandBuffer* command{};LLGL::Fence* fence{};
    template<class T>LLGL::Shader* shader(const T& code,LLGL::ShaderType type){LLGL::ShaderDescriptor d{};d.type=type;d.source=reinterpret_cast<const char*>(code.data());d.sourceSize=code.size()*sizeof(code[0]);d.sourceType=LLGL::ShaderSourceType::BinaryBuffer;d.entryPoint="main";auto p=c.renderer->CreateShader(d);require(p&&(!p->GetReport()||!p->GetReport()->HasErrors()),"SPIR-V shader create failed");return p;}
    Draw(Context& context):c(context){
        using namespace drawbench;
        LLGL::TextureDescriptor t{};t.extent={width,height,1};t.format=LLGL::Format::RGBA8UNorm;t.mipLevels=1;t.bindFlags=LLGL::BindFlags::ColorAttachment|LLGL::BindFlags::Sampled|LLGL::BindFlags::CopySrc;t.miscFlags=LLGL::MiscFlags::NoInitialData;texture=c.renderer->CreateTexture(t);
        LLGL::RenderTargetDescriptor rt{};rt.resolution={width,height};rt.colorAttachments[0]=LLGL::AttachmentDescriptor{texture};target=c.renderer->CreateRenderTarget(rt);require(target!=nullptr,"Offscreen target create failed");pass=target->GetRenderPass();
        vs=shader(fixtures::fullscreen,LLGL::ShaderType::Vertex);rs=shader(fixtures::red,LLGL::ShaderType::Fragment);gs=shader(fixtures::green,LLGL::ShaderType::Fragment);
        LLGL::GraphicsPipelineDescriptor state{};state.vertexShader=vs;state.fragmentShader=rs;state.renderPass=pass;state.rasterizer.cullMode=LLGL::CullMode::Disabled;state.rasterizer.scissorTestEnabled=true;red=c.renderer->CreatePipelineState(state);state.fragmentShader=gs;green=c.renderer->CreatePipelineState(state);
        for(auto pipeline:{red,green})require(pipeline&&(!pipeline->GetReport()||!pipeline->GetReport()->HasErrors()),"Graphics pipeline create failed");
        LLGL::BufferDescriptor b{};b.size=width*height*4;b.bindFlags=LLGL::BindFlags::CopyDst;b.cpuAccessFlags=LLGL::CPUAccessFlags::Read;readback=c.renderer->CreateBuffer(b);command=c.renderer->CreateCommandBuffer({});fence=c.renderer->CreateFence();require(texture&&pass&&target&&readback&&command&&fence,"Draw setup failed");require(target->GetSamples()==1,"Unexpected multisampling");
    }
    void complete(){auto& q=*c.renderer->GetCommandQueue();q.Submit(*command);q.Submit(*fence);require(q.WaitFence(*fence,5'000'000'000ull),"Draw/readback fence timeout");}
    std::array<double,4> draw(drawbench::Work w,unsigned iteration,const std::string& control){
        using namespace drawbench;const float clear[]{0,iteration%2?1.f:0.f,1,1};auto pipeline=iteration%2?green:red;auto& q=*c.renderer->GetCommandQueue();auto calls=w.calls-(control=="wrong-call-count"?1:0);auto instances=w.instances-(control=="wrong-instance-count"?1:0);auto a=bench::Clock::now();command->Begin();command->BeginRenderPass(*target);command->Clear(LLGL::ClearFlags::Color,clear);command->SetViewport(LLGL::Extent2D{width,height});command->SetPipelineState(*pipeline);
        command->SetScissor({int(left)+(control=="wrong-scissor"?1:0),int(top),int(right-left),int(bottom-top)});

        for(unsigned i=0;i<calls;++i){if(w.instances==1)command->Draw(3,0);else command->DrawInstanced(3,0,instances,0);}command->EndRenderPass();command->End();auto b=bench::Clock::now();q.Submit(*command);q.Submit(*fence);auto submitted=bench::Clock::now();require(q.WaitFence(*fence,5'000'000'000ull),"Completed draw fence timeout");auto done=bench::Clock::now();return{bench::ms(a,b),bench::ms(b,submitted),bench::ms(submitted,done),bench::ms(a,done)};
    }
    std::vector<std::byte> read(){LLGL::TextureRegion region{};region.extent={drawbench::width,drawbench::height,1};command->Begin();command->CopyBufferFromTexture(*readback,0,*texture,region,0,0);command->End();complete();std::vector<std::byte> bytes(drawbench::width*drawbench::height*4);c.renderer->ReadBuffer(*readback,0,bytes.data(),bytes.size());return bytes;}
    void close(){if(!texture)return;c.renderer->GetCommandQueue()->WaitIdle();for(auto p:{red,green})c.renderer->Release(*p);for(auto p:{vs,rs,gs})c.renderer->Release(*p);c.renderer->Release(*command);c.renderer->Release(*fence);c.renderer->Release(*readback);c.renderer->Release(*target);c.renderer->Release(*texture);texture=nullptr;}
    ~Draw(){close();}
};
}
