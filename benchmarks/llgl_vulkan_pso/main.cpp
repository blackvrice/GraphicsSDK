#include "Support.hpp"
#include "../vulkan_pso/Frame.hpp"
#include "../../tests/fixtures/SpirvFixtures.hpp"
#include <set>

#ifndef NDEBUG
#error Release required
#endif
namespace llglpso {
struct Totals{unsigned created{},released{},used{},shaderCreated{},shaderReleased{};};
struct PSO {
    Context& c;Totals totals;LLGL::Texture* texture{};LLGL::RenderTarget* target{};const LLGL::RenderPass* pass{};LLGL::Buffer* readback{};LLGL::CommandBuffer* readCommand{};LLGL::Fence* readFence{};std::vector<LLGL::PipelineState*> pipelines;
    PSO(Context& context):c(context){using namespace psobench;
        LLGL::TextureDescriptor t{};t.extent={width,height,1};t.format=LLGL::Format::RGBA8UNorm;t.mipLevels=1;t.bindFlags=LLGL::BindFlags::ColorAttachment|LLGL::BindFlags::Sampled|LLGL::BindFlags::CopySrc;t.miscFlags=LLGL::MiscFlags::NoInitialData;texture=c.renderer->CreateTexture(t);
        LLGL::RenderTargetDescriptor rt{};rt.resolution={width,height};rt.colorAttachments[0]=LLGL::AttachmentDescriptor{texture};target=c.renderer->CreateRenderTarget(rt);require(target!=nullptr,"Offscreen target create failed");pass=target->GetRenderPass();require(target->GetSamples()==1,"Unexpected multisampling");
        LLGL::BufferDescriptor b{};b.size=width*height*4;b.bindFlags=LLGL::BindFlags::CopyDst;b.cpuAccessFlags=LLGL::CPUAccessFlags::Read;readback=c.renderer->CreateBuffer(b);readCommand=c.renderer->CreateCommandBuffer({});readFence=c.renderer->CreateFence();require(texture&&pass&&readback&&readCommand&&readFence,"PSO setup failed");
    }
    LLGL::Shader* shader(std::span<const std::uint32_t> words,LLGL::ShaderType stage){LLGL::ShaderDescriptor d{};d.type=stage;d.source=reinterpret_cast<const char*>(words.data());d.sourceSize=words.size_bytes();d.sourceType=LLGL::ShaderSourceType::BinaryBuffer;d.entryPoint="main";auto p=c.renderer->CreateShader(d);require(p&&(!p->GetReport()||!p->GetReport()->HasErrors()),"SPIR-V shader create failed");++totals.shaderCreated;return p;}
    void create(unsigned count,unsigned iteration,const std::string& control){
        require(pipelines.empty(),"Previous pipelines still retained");pipelines.resize(count);std::set<LLGL::PipelineState*> unique;
        for(unsigned i=0;i<count;++i){
            if(control=="missing-pipeline"&&i+1==count)continue;
            auto vs=shader(fixtures::fullscreen,LLGL::ShaderType::Vertex);auto fs=shader((iteration+i)%2?std::span<const std::uint32_t>(fixtures::green):std::span<const std::uint32_t>(fixtures::red),LLGL::ShaderType::Fragment);
            LLGL::GraphicsPipelineDescriptor state{};state.vertexShader=vs;state.fragmentShader=fs;state.renderPass=pass;state.rasterizer.cullMode=LLGL::CullMode::Disabled;state.rasterizer.scissorTestEnabled=true;
            auto p=c.renderer->CreatePipelineState(state);require(p&&(!p->GetReport()||!p->GetReport()->HasErrors()),"Graphics PSO create failed");pipelines[i]=p;++totals.created;require(unique.insert(p).second,"Duplicate live public PSO handle");
            c.renderer->Release(*fs);c.renderer->Release(*vs);totals.shaderReleased+=2;
        }
        require(unique.size()==count,"Public pipeline creation count differs");
    }
    void use(unsigned count,unsigned iteration,const std::string& control){using namespace psobench;
        auto command=c.renderer->CreateCommandBuffer({});auto fence=c.renderer->CreateFence();require(command&&fence,"PSO use command create failed");
        command->Begin();command->BeginRenderPass(*target);const float clear[]{0,iteration%2?1.f:0.f,1,1};command->Clear(LLGL::ClearFlags::Color,clear);command->SetViewport(LLGL::Extent2D{width,height});
        for(unsigned i=0;i<count;++i){if(control=="skip-region"&&i+1==count)continue;unsigned index=control=="wrong-pipeline"?(i+1)%count:i;command->SetPipelineState(*pipelines[index]);
            command->SetScissor({int(edge(i,count))+(control=="wrong-scissor"?1:0),int(top),int(edge(i+1,count)-edge(i,count)),int(bottom-top)});command->Draw(3,0);++totals.used;}
        command->EndRenderPass();command->End();auto& q=*c.renderer->GetCommandQueue();q.Submit(*command);q.Submit(*fence);require(q.WaitFence(*fence,5'000'000'000ull),"PSO GPU use fence timeout");q.WaitIdle();c.renderer->Release(*command);c.renderer->Release(*fence);
    }
    void release(unsigned count,const std::string& control){unsigned released{};
        for(unsigned i=0;i<pipelines.size();++i){if(control=="skip-release"&&i+1==count)continue;if(pipelines[i]){c.renderer->Release(*pipelines[i]);pipelines[i]=nullptr;++released;++totals.released;}}
        require(released==count,"Public pipeline release count differs");pipelines.clear();
    }
    std::vector<std::byte> read(){using namespace psobench;LLGL::TextureRegion region{};region.extent={width,height,1};readCommand->Begin();readCommand->CopyBufferFromTexture(*readback,0,*texture,region,0,0);readCommand->End();auto& q=*c.renderer->GetCommandQueue();q.Submit(*readCommand);q.Submit(*readFence);require(q.WaitFence(*readFence,5'000'000'000ull),"PSO readback fence timeout");std::vector<std::byte> bytes(width*height*4);c.renderer->ReadBuffer(*readback,0,bytes.data(),bytes.size());return bytes;}
    void close(){if(!texture)return;c.renderer->GetCommandQueue()->WaitIdle();for(auto p:pipelines)if(p)c.renderer->Release(*p);pipelines.clear();c.renderer->Release(*readCommand);c.renderer->Release(*readFence);c.renderer->Release(*readback);c.renderer->Release(*target);c.renderer->Release(*texture);texture=nullptr;}
    ~PSO(){close();}
};
}
int main(int argc,char** argv)try{
    using namespace llglpso;using namespace psobench;require(argc==4,"Usage: llgl_vulkan_pso EXPECTED_GPU plain|validation pso1|pso8|CONTROL");std::string expected=argv[1],mode=argv[2],name=argv[3];require(!expected.empty()&&(mode=="plain"||mode=="validation"),"Invalid arguments");bool validation=mode=="validation",diagnostic=name=="core-control"||name=="sync-control";
    bool custom=name=="missing-pipeline"||name=="skip-release"||name=="wrong-pipeline"||name=="skip-region"||name=="wrong-scissor"||name=="corrupt-readback";unsigned count=diagnostic?0:work(custom?"pso8":name);
    Context c(validation,expected);Observer observer;auto deviceIdentity=identity(c);std::vector<std::string> hashes;std::string token;Totals totals;
    if(diagnostic)token=validationControl(c,name);
    else{PSO adapter(c);observer.reset();
        for(unsigned i=0;i<4;++i){auto before=observer.snapshot();adapter.create(count,i,name);adapter.use(count,i,name);adapter.release(count,name);auto bytes=adapter.read();if(name=="corrupt-readback")bytes.back()^=std::byte{1};auto after=observer.snapshot();
            require(after.calls-before.calls==count&&after.forwarded-before.forwarded==count&&after.instances-before.instances==count&&after.vertices-before.vertices==count*3&&after.badArguments==0,"Native PSO draw count/arguments differ");require(bytes==reference(i,count),"Full changing PSO stripe reference mismatch");hashes.push_back(sha256(bytes));}
        totals=adapter.totals;adapter.close();require(totals.created==count*4&&totals.released==count*4&&totals.used==count*4&&totals.shaderCreated==count*8&&totals.shaderReleased==count*8,"Public PSO lifecycle counters differ");}
    auto observed=observer.snapshot();auto runtime=modules();c.close();auto retired=observer.snapshot();require(retired.liveInstances==0&&retired.liveDevices==0,"Observer observed live native objects after teardown");if(!diagnostic)require(errors==0,"Native validation error delivered");
    std::cout<<"{\"schema_version\":1,\"task\":\"P3-CMP-05\",\"sdk\":\"llgl\",\"api\":\"vulkan\",\"configuration\":\"Release\",\"profile_id\":\"llgl-fixed-pin-transfer-src-v1\",\"source_fingerprint\":"<<quote(GFX_LLGL_PSO_FINGERPRINT)<<",\"shader_header_sha256\":"<<quote(GFX_PSO_SHADER_SHA)<<','<<deviceIdentity
      <<",\"contract\":\"vulkan_pipeline_create_public_release_use_checked_v1\",\"category\":\"cpu_api\",\"scenario\":"<<quote(name)<<",\"validation\":"<<(validation?"true":"false")<<",\"control\":"<<(diagnostic?"true":"false")<<",\"control_diagnostic\":"<<quote(token)<<",\"delivered_errors\":"<<errors
      <<",\"clear_strategy\":\"automatic_offscreen_pass_plus_full_attachment_clear\",\"texture_bind_policy\":\"color_attachment_sampled_copy_src\",\"width\":65,\"height\":37,\"texel_bytes\":4,\"scissor\":[5,3,55,31],\"vertices_per_pipeline\":3,\"first_vertex\":0,\"first_instance\":0,\"pipelines_per_iteration\":"<<count<<",\"created_pipelines\":"<<totals.created<<",\"released_pipeline_handles\":"<<totals.released<<",\"used_pipeline_regions\":"<<totals.used<<",\"created_shader_handles\":"<<totals.shaderCreated<<",\"released_shader_handles\":"<<totals.shaderReleased
      <<",\"includes_shader_object_create_release\":true,\"GPU_use_in_timing\":false,\"GPU_memory_reclamation_verified\":false,\"native_pipeline_count_verified\":false,\"driver_cache_state_known\":false,\"cache_state\":\"uncontrolled_driver_cache_repeated_public_creation\",\"warmup\":"<<(diagnostic?0:1)<<",\"sample_count\":"<<(diagnostic?0:3)<<",\"verified_images\":"<<hashes.size()<<",\"verified_bytes\":"<<hashes.size()*width*height*4<<",\"readback_verified\":"<<(diagnostic?"false":"true")<<",\"frame_sha256\":[";
    for(unsigned i=0;i<hashes.size();++i){if(i)std::cout<<',';std::cout<<quote(hashes[i]);}
    std::cout<<"],\"native_draw_calls\":"<<observed.calls<<",\"native_instances\":"<<observed.instances<<",\"native_vertices\":"<<observed.vertices<<",\"native_forwarded_draw_calls\":"<<observed.forwarded<<",\"native_bad_arguments\":"<<observed.badArguments<<",\"observer_path\":"<<quote(observer.path)<<",\"observed_instance_api_version\":"<<observed.instanceApi<<",\"observer_live_instances_after_close\":"<<retired.liveInstances<<",\"observer_live_devices_after_close\":"<<retired.liveDevices
      <<",\"runtime_modules\":"<<runtime<<",\"submitted_control_work\":0,\"performance_samples\":0,\"samples_ms\":[],\"gpu_ms\":null,\"performance_accepted\":false,\"native_workload_shortcuts\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
