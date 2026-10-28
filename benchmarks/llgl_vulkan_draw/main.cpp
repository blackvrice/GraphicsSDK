#include "../llgl_vulkan_upload/Context.hpp"
#include "../vulkan_draw/Frame.hpp"
#include "../../tests/fixtures/SpirvFixtures.hpp"
#include "Observer.hpp"
#include <bcrypt.h>
#include <span>

#ifndef NDEBUG
#error Release required
#endif
namespace llgldraw {
using namespace llglupload;
std::string sha256(std::span<const std::byte> data){BCRYPT_ALG_HANDLE algorithm{};require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider failed");std::array<unsigned char,32> out{};auto r=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data.data())),ULONG(data.size()),out.data(),ULONG(out.size()));BCryptCloseAlgorithmProvider(algorithm,0);require(r>=0,"SHA256 failed");return hex(out);}
std::string identity(Context& c){auto& p=c.properties.properties;std::ostringstream o;
    o<<"\"device_name\":"<<quote(p.deviceName)<<",\"device_uuid\":"<<quote(hex(c.id.deviceUUID))<<",\"driver_uuid\":"<<quote(hex(c.id.driverUUID))<<",\"device_luid\":"<<quote(hex(c.id.deviceLUID))
     <<",\"native_vendor_id\":"<<p.vendorID<<",\"native_device_id\":"<<p.deviceID<<",\"native_driver_version\":"<<p.driverVersion<<",\"native_api_version\":"<<p.apiVersion<<",\"native_handle_verified\":true"
     <<",\"queue_family\":"<<c.native.queueFamily<<",\"queue_index\":0,\"queue_flags\":"<<c.flags<<",\"loader_api_version\":"<<c.loaderVersion<<",\"instance_API_request_observed\":true"
     <<",\"loader_path\":"<<quote(c.loader)<<",\"LLGL_path\":"<<quote(c.core)<<",\"backend_path\":"<<quote(c.backend)<<",\"loaded_validation_layer\":"<<quote(c.loadedLayer);return o.str();}
struct Observer {
    HMODULE library{};drawobserver::Read read{};drawobserver::Reset reset{};std::string path;
    Observer(){auto loaded=GetModuleHandleW(L"VkLayer_gfx_llgl_draw.dll");require(loaded!=nullptr,"Native draw observer not active");path=module(loaded);library=LoadLibraryW(std::filesystem::path(path).c_str());require(library!=nullptr,"Observer retain failed");read=reinterpret_cast<drawobserver::Read>(GetProcAddress(library,"gfxDrawSnapshot"));reset=reinterpret_cast<drawobserver::Reset>(GetProcAddress(library,"gfxDrawReset"));require(read&&reset,"Observer snapshot entry missing");auto s=snapshot();require(s.createdInstances==1&&s.createdDevices==1&&s.liveInstances==1&&s.liveDevices==1&&s.instanceApi!=0,"Native draw observer not active");}
    drawobserver::Snapshot snapshot(){drawobserver::Snapshot s{};require(read(&s,sizeof(s))!=0,"Native snapshot failed");return s;}
    ~Observer(){if(library)FreeLibrary(library);}
};
std::string validationControl(Context& c,const std::string& name){
    require(!c.loadedLayer.empty(),"Controls require validation");
    if(name=="core-control"){VkPhysicalDeviceProperties2 invalid{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};c.getProperties(c.native.physicalDevice,&invalid);}
    else{
        LLGL::BufferDescriptor a{};a.size=4096;a.bindFlags=LLGL::BindFlags::CopySrc;auto b=a;b.bindFlags=LLGL::BindFlags::CopyDst;auto src=c.renderer->CreateBuffer(a);auto dst=c.renderer->CreateBuffer(b);auto command=c.renderer->CreateCommandBuffer({});require(src&&dst&&command,"Control resources unavailable");
        LLGL::Vulkan::ResourceNativeHandle sn{},dn{};require(src->GetNativeHandle(&sn,sizeof(sn))&&dst->GetNativeHandle(&dn,sizeof(dn)),"Native control buffers unavailable");command->Begin();LLGL::Vulkan::CommandBufferNativeHandle cn{};require(command->GetNativeHandle(&cn,sizeof(cn)),"Native control command unavailable");
        // Validation-only negative control, never submitted to the GPU.
        VkBufferCopy copy{0,0,4096};vkCmdCopyBuffer(cn.commandBuffer,sn.buffer.buffer,dn.buffer.buffer,1,&copy);vkCmdCopyBuffer(cn.commandBuffer,sn.buffer.buffer,dn.buffer.buffer,1,&copy);command->End();c.renderer->Release(*command);c.renderer->Release(*dst);c.renderer->Release(*src);
    }
    std::string token=name=="core-control"?"VUID-VkPhysicalDeviceProperties2-sType-sType":"SYNC-HAZARD-WRITE-AFTER-WRITE";
    require(std::any_of(errorIds.begin(),errorIds.end(),[&](auto& s){return s.find(token)!=std::string::npos;}),"Expected validation control diagnostic absent");return token;
}
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
    void draw(drawbench::Work w,unsigned iteration,const std::string& control){
        using namespace drawbench;const float clear[]{0,iteration%2?1.f:0.f,1,1};command->Begin();command->BeginRenderPass(*target);command->Clear(LLGL::ClearFlags::Color,clear);command->SetViewport(LLGL::Extent2D{width,height});command->SetPipelineState(iteration%2?*green:*red);
        command->SetScissor({int(left)+(control=="wrong-scissor"?1:0),int(top),int(right-left),int(bottom-top)});
        auto calls=w.calls-(control=="wrong-call-count"?1:0);auto instances=w.instances-(control=="wrong-instance-count"?1:0);
        for(unsigned i=0;i<calls;++i){if(w.instances==1)command->Draw(3,0);else command->DrawInstanced(3,0,instances,0);}command->EndRenderPass();command->End();complete();
    }
    std::vector<std::byte> read(){LLGL::TextureRegion region{};region.extent={drawbench::width,drawbench::height,1};command->Begin();command->CopyBufferFromTexture(*readback,0,*texture,region,0,0);command->End();complete();std::vector<std::byte> bytes(drawbench::width*drawbench::height*4);c.renderer->ReadBuffer(*readback,0,bytes.data(),bytes.size());return bytes;}
    void close(){if(!texture)return;c.renderer->GetCommandQueue()->WaitIdle();for(auto p:{red,green})c.renderer->Release(*p);for(auto p:{vs,rs,gs})c.renderer->Release(*p);c.renderer->Release(*command);c.renderer->Release(*fence);c.renderer->Release(*readback);c.renderer->Release(*target);c.renderer->Release(*texture);texture=nullptr;}
    ~Draw(){close();}
};
}
int main(int argc,char** argv)try{
    using namespace llgldraw;using namespace drawbench;require(argc==4,"Usage: llgl_vulkan_draw EXPECTED_GPU plain|validation SCENARIO|CONTROL");std::string expected=argv[1],mode=argv[2],name=argv[3];require(!expected.empty()&&(mode=="plain"||mode=="validation"),"Invalid arguments");bool validation=mode=="validation",diagnostic=name=="core-control"||name=="sync-control";
    std::string scenario=name=="wrong-call-count"?"draw1000":name=="wrong-instance-count"?"instanced1000":(name=="wrong-scissor"||name=="corrupt-readback")?"draw1":name;auto w=diagnostic?Work{}:work(scenario);
    Context c(validation,expected);Observer observer;auto deviceIdentity=identity(c);std::vector<std::string> hashes;std::string token;
    if(diagnostic)token=validationControl(c,name);
    else{
        Draw adapter(c);observer.reset();
        for(unsigned i=0;i<4;++i){auto before=observer.snapshot();adapter.draw(w,i,name);auto bytes=adapter.read();if(name=="corrupt-readback")bytes.back()^=std::byte{1};auto after=observer.snapshot();
            require(after.calls-before.calls==w.calls&&after.forwarded-before.forwarded==w.calls&&after.instances-before.instances==std::uint64_t(w.calls)*w.instances&&after.vertices-before.vertices==std::uint64_t(3)*w.calls*w.instances&&after.badArguments==0,"Native draw count/arguments differ from contract");
            require(bytes==reference(i),"Full changing draw reference mismatch");hashes.push_back(sha256(bytes));}
        adapter.close();
    }
    auto observed=observer.snapshot();auto runtime=modules();c.close();auto retired=observer.snapshot();require(retired.liveInstances==0&&retired.liveDevices==0,"Observer observed live native objects after teardown");if(!diagnostic)require(errors==0,"Native validation error delivered");
    std::cout<<"{\"schema_version\":1,\"task\":\"P3-CMP-04\",\"sdk\":\"llgl\",\"api\":\"vulkan\",\"configuration\":\"Release\",\"profile_id\":\"llgl-fixed-pin-transfer-src-v1\",\"source_fingerprint\":"<<quote(GFX_LLGL_DRAW_FINGERPRINT)<<",\"shader_header_sha256\":"<<quote(GFX_DRAW_SHADER_SHA)<<','<<deviceIdentity
      <<",\"contract\":\"vulkan_scissored_fullscreen_triangle_draw_fence_each_sample_readback_v1\",\"scenario\":"<<quote(name)<<",\"validation\":"<<(validation?"true":"false")<<",\"control\":"<<(diagnostic?"true":"false")<<",\"control_diagnostic\":"<<quote(token)<<",\"delivered_errors\":"<<errors
      <<",\"clear_strategy\":\"automatic_offscreen_pass_plus_full_attachment_clear\",\"texture_bind_policy\":\"color_attachment_sampled_copy_src\",\"width\":65,\"height\":37,\"texel_bytes\":4,\"scissor\":[5,3,55,31],\"vertices_per_instance\":3,\"first_vertex\":0,\"first_instance\":0,\"draw_calls_per_frame\":"<<w.calls<<",\"instances_per_call\":"<<w.instances<<",\"warmup\":"<<(diagnostic?0:1)<<",\"sample_count\":"<<(diagnostic?0:3)<<",\"verified_images\":"<<hashes.size()<<",\"verified_bytes\":"<<hashes.size()*width*height*4<<",\"readback_verified\":"<<(diagnostic?"false":"true")<<",\"frame_sha256\":[";
    for(unsigned i=0;i<hashes.size();++i){if(i)std::cout<<',';std::cout<<quote(hashes[i]);}
    std::cout<<"],\"native_draw_calls\":"<<observed.calls<<",\"native_instances\":"<<observed.instances<<",\"native_vertices\":"<<observed.vertices<<",\"native_forwarded_draw_calls\":"<<observed.forwarded<<",\"native_bad_arguments\":"<<observed.badArguments<<",\"native_min_instances\":"<<observed.minInstances<<",\"native_max_instances\":"<<observed.maxInstances
      <<",\"observer_path\":"<<quote(observer.path)<<",\"observed_instance_api_version\":"<<observed.instanceApi<<",\"observer_live_instances_after_close\":"<<retired.liveInstances<<",\"observer_live_devices_after_close\":"<<retired.liveDevices
      <<",\"runtime_modules\":"<<runtime<<",\"submitted_control_work\":0,\"performance_samples\":0,\"samples_ms\":[],\"CPU_intervals_ms\":[],\"gpu_ms\":null,\"performance_accepted\":false,\"native_workload_shortcuts\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
