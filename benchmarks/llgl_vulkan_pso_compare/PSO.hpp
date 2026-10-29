#pragma once
#include "../llgl_vulkan_pso/Support.hpp"
#include "../vulkan_pso/Frame.hpp"
#include "../../tests/fixtures/SpirvFixtures.hpp"
#include "../common/Adapter.hpp"
#include <set>
namespace llglpso {
struct Totals{unsigned created{},released{},used{},shaderCreated{},shaderReleased{};};
struct PSO {
    Context& c;Totals totals;LLGL::Texture* texture{};LLGL::RenderTarget* target{};const LLGL::RenderPass* pass{};LLGL::Buffer* readback{};LLGL::CommandBuffer* readCommand{};LLGL::Fence* readFence{};std::vector<LLGL::PipelineState*> pipelines;
    PSO(Context& context):c(context){using namespace psobench;
        LLGL::TextureDescriptor t{};t.extent={width,height,1};t.format=LLGL::Format::RGBA8UNorm;t.mipLevels=1;t.bindFlags=LLGL::BindFlags::ColorAttachment|LLGL::BindFlags::Sampled|LLGL::BindFlags::CopySrc;t.miscFlags=LLGL::MiscFlags::NoInitialData;texture=c.renderer->CreateTexture(t);
        LLGL::RenderTargetDescriptor rt{};rt.resolution={width,height};rt.colorAttachments[0]=LLGL::AttachmentDescriptor{texture};target=c.renderer->CreateRenderTarget(rt);require(target!=nullptr,"Offscreen target create failed");pass=target->GetRenderPass();require(target->GetSamples()==1,"Unexpected multisampling");
        LLGL::BufferDescriptor b{};b.size=width*height*4;b.bindFlags=LLGL::BindFlags::CopyDst;b.cpuAccessFlags=LLGL::CPUAccessFlags::Read;readback=c.renderer->CreateBuffer(b);readCommand=c.renderer->CreateCommandBuffer({});readFence=c.renderer->CreateFence();require(texture&&pass&&readback&&readCommand&&readFence,"PSO setup failed");
    }
    LLGL::Shader* shader(const LLGL::ShaderDescriptor& d){auto p=c.renderer->CreateShader(d);require(p&&(!p->GetReport()||!p->GetReport()->HasErrors()),"SPIR-V shader create failed");return p;}
    double create(unsigned count,unsigned iteration,const std::string& control){
        require(pipelines.empty(),"Previous pipelines still retained");pipelines.resize(count);
        auto descriptor=[](std::span<const std::uint32_t> words,LLGL::ShaderType stage){LLGL::ShaderDescriptor d{};d.type=stage;d.source=reinterpret_cast<const char*>(words.data());d.sourceSize=words.size_bytes();d.sourceType=LLGL::ShaderSourceType::BinaryBuffer;d.entryPoint="main";return d;};
        auto vertex=descriptor(fixtures::fullscreen,LLGL::ShaderType::Vertex);std::vector<LLGL::ShaderDescriptor> fragments(count);std::vector<LLGL::GraphicsPipelineDescriptor> descriptions(count);
        for(unsigned i=0;i<count;++i){fragments[i]=descriptor((iteration+i)%2?std::span<const std::uint32_t>(fixtures::green):std::span<const std::uint32_t>(fixtures::red),LLGL::ShaderType::Fragment);auto& d=descriptions[i];d.renderPass=pass;d.rasterizer.cullMode=LLGL::CullMode::Disabled;d.rasterizer.scissorTestEnabled=true;}
        unsigned limit=count-(control=="missing-pipeline"?1:0);auto begin=bench::Clock::now();
        for(unsigned i=0;i<limit;++i){auto vs=shader(vertex),fs=shader(fragments[i]);auto& d=descriptions[i];d.vertexShader=vs;d.fragmentShader=fs;auto p=c.renderer->CreatePipelineState(d);require(p&&(!p->GetReport()||!p->GetReport()->HasErrors()),"Graphics PSO create failed");pipelines[i]=p;c.renderer->Release(*fs);c.renderer->Release(*vs);}
        auto end=bench::Clock::now();totals.created+=limit;totals.shaderCreated+=2*limit;totals.shaderReleased+=2*limit;
        std::set<LLGL::PipelineState*> unique;for(auto p:pipelines)if(p)require(unique.insert(p).second,"Duplicate live public PSO handle");require(unique.size()==count,"Public pipeline creation count differs");return bench::ms(begin,end);
    }
    void use(unsigned count,unsigned iteration,const std::string& control){using namespace psobench;
        auto command=c.renderer->CreateCommandBuffer({});auto fence=c.renderer->CreateFence();require(command&&fence,"PSO use command create failed");
        command->Begin();command->BeginRenderPass(*target);const float clear[]{0,iteration%2?1.f:0.f,1,1};command->Clear(LLGL::ClearFlags::Color,clear);command->SetViewport(LLGL::Extent2D{width,height});
        for(unsigned i=0;i<count;++i){if(control=="skip-region"&&i+1==count)continue;unsigned index=control=="wrong-pipeline"?(i+1)%count:i;command->SetPipelineState(*pipelines[index]);
            command->SetScissor({int(edge(i,count))+(control=="wrong-scissor"?1:0),int(top),int(edge(i+1,count)-edge(i,count)),int(bottom-top)});command->Draw(3,0);++totals.used;}
        command->EndRenderPass();command->End();auto& q=*c.renderer->GetCommandQueue();q.Submit(*command);q.Submit(*fence);require(q.WaitFence(*fence,5'000'000'000ull),"PSO GPU use fence timeout");q.WaitIdle();c.renderer->Release(*command);c.renderer->Release(*fence);
    }
    double release(unsigned count,const std::string& control){
        require(pipelines.size()==count&&std::all_of(pipelines.begin(),pipelines.end(),[](auto p){return p!=nullptr;}),"Invalid live public pipeline handles");unsigned limit=count-(control=="skip-release"?1:0);
        auto begin=bench::Clock::now();for(unsigned i=0;i<limit;++i)c.renderer->Release(*pipelines[i]);auto end=bench::Clock::now();
        for(unsigned i=0;i<limit;++i)pipelines[i]=nullptr;totals.released+=limit;require(limit==count,"Public pipeline release count differs");pipelines.clear();return bench::ms(begin,end);
    }
    std::vector<std::byte> read(){using namespace psobench;LLGL::TextureRegion region{};region.extent={width,height,1};readCommand->Begin();readCommand->CopyBufferFromTexture(*readback,0,*texture,region,0,0);readCommand->End();auto& q=*c.renderer->GetCommandQueue();q.Submit(*readCommand);q.Submit(*readFence);require(q.WaitFence(*readFence,5'000'000'000ull),"PSO readback fence timeout");std::vector<std::byte> bytes(width*height*4);c.renderer->ReadBuffer(*readback,0,bytes.data(),bytes.size());return bytes;}
    void close(){if(!texture)return;c.renderer->GetCommandQueue()->WaitIdle();for(auto p:pipelines)if(p)c.renderer->Release(*p);pipelines.clear();c.renderer->Release(*readCommand);c.renderer->Release(*readFence);c.renderer->Release(*readback);c.renderer->Release(*target);c.renderer->Release(*texture);texture=nullptr;}
    ~PSO(){close();}
};
}
