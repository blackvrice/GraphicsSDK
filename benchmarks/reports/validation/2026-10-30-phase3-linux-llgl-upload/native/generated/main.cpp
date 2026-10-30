#include "Platform.hpp"
#include "Payload.hpp"
#include <openssl/sha.h>
#include <span>

#ifndef NDEBUG
#error Release required
#endif
namespace llglupload {
std::string sha256(std::span<const std::byte> data){
    std::array<unsigned char,32> digest{};
    require(SHA256(reinterpret_cast<const unsigned char*>(data.data()),data.size(),digest.data())!=nullptr,"SHA256 failed");
    return hex(digest);
}
std::string validationControl(Context& c,const std::string& kind){
    require(!c.loadedLayer.empty(),"Controls require validation");
    if(kind=="core-control"){
        VkPhysicalDeviceProperties2 invalid{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};c.getProperties(c.native.physicalDevice,&invalid);
    }else{
        LLGL::BufferDescriptor a{};a.size=4096;a.bindFlags=LLGL::BindFlags::CopySrc;auto b=a;b.bindFlags=LLGL::BindFlags::CopyDst;
        auto src=c.renderer->CreateBuffer(a);auto dst=c.renderer->CreateBuffer(b);auto command=c.renderer->CreateCommandBuffer({});require(src&&dst&&command,"Control resource create failed");
        LLGL::Vulkan::ResourceNativeHandle sn{},dn{};require(src->GetNativeHandle(&sn,sizeof(sn))&&dst->GetNativeHandle(&dn,sizeof(dn)),"Control resource native handles unavailable");
        command->Begin();LLGL::Vulkan::CommandBufferNativeHandle cn{};require(command->GetNativeHandle(&cn,sizeof(cn)),"Control command native handle unavailable");
        // Validation-only negative control: record a write-after-write, never submit.
        VkBufferCopy copy{0,0,4096};vkCmdCopyBuffer(cn.commandBuffer,sn.buffer.buffer,dn.buffer.buffer,1,&copy);vkCmdCopyBuffer(cn.commandBuffer,sn.buffer.buffer,dn.buffer.buffer,1,&copy);
        command->End();c.renderer->Release(*command);c.renderer->Release(*dst);c.renderer->Release(*src);
    }
    const std::string token=kind=="core-control"?"VUID-VkPhysicalDeviceProperties2-sType-sType":"SYNC-HAZARD-WRITE-AFTER-WRITE";
    require(std::any_of(errorIds.begin(),errorIds.end(),[&](auto& s){return s.find(token)!=std::string::npos;}),"Expected validation control diagnostic absent");
    return token;
}
struct Upload {
    Context& c;LLGL::Buffer *staging{},*readback{};LLGL::Texture* texture{};LLGL::CommandBuffer* command{};LLGL::Fence* fence{};LLGL::TextureRegion region{};
    Upload(Context& context,Shape s):c(context){
        LLGL::TextureDescriptor desc{};desc.type=LLGL::TextureType::Texture2D;desc.format=s.bpp==8?LLGL::Format::RGBA16Float:LLGL::Format::RGBA8UNorm;desc.extent={s.w,s.h,1};desc.mipLevels=1;desc.arrayLayers=1;desc.samples=1;desc.bindFlags=LLGL::BindFlags::Sampled|LLGL::BindFlags::CopySrc|LLGL::BindFlags::CopyDst;
        texture=c.renderer->CreateTexture(desc);LLGL::BufferDescriptor buffer{};buffer.size=std::uint64_t(s.w)*s.h*s.bpp;buffer.bindFlags=LLGL::BindFlags::CopySrc;buffer.cpuAccessFlags=LLGL::CPUAccessFlags::Write;staging=c.renderer->CreateBuffer(buffer);
        buffer.bindFlags=LLGL::BindFlags::CopyDst;buffer.cpuAccessFlags=LLGL::CPUAccessFlags::Read;readback=c.renderer->CreateBuffer(buffer);command=c.renderer->CreateCommandBuffer({});fence=c.renderer->CreateFence();region.extent={s.w,s.h,1};
        require(texture&&staging&&readback&&command&&fence,"Upload resources unavailable");
    }
    void complete(){auto& q=*c.renderer->GetCommandQueue();q.Submit(*command);q.Submit(*fence);require(q.WaitFence(*fence,5'000'000'000ull),"Completed upload/readback fence timeout");}
    void write(std::span<const std::byte> pixels){
        auto mapped=c.renderer->MapBuffer(*staging,LLGL::CPUAccess::WriteOnly);require(mapped!=nullptr,"Upload map failed");std::memcpy(mapped,pixels.data(),pixels.size());c.renderer->UnmapBuffer(*staging);
        command->Begin();command->CopyTextureFromBuffer(*texture,region,*staging,0,0,0);command->End();complete();
    }
    std::vector<std::byte> read(std::size_t size){
        command->Begin();command->CopyBufferFromTexture(*readback,0,*texture,region,0,0);command->End();complete();std::vector<std::byte> result(size);c.renderer->ReadBuffer(*readback,0,result.data(),result.size());return result;
    }
    void close(){if(!texture)return;c.renderer->GetCommandQueue()->WaitIdle();c.renderer->Release(*command);c.renderer->Release(*fence);c.renderer->Release(*readback);c.renderer->Release(*staging);c.renderer->Release(*texture);texture=nullptr;}
    ~Upload(){close();}
};
}
int main(int argc,char** argv)try{
    using namespace llglupload;
    require(argc==4,"Usage: linux_llgl_upload EXPECTED_UUID plain|validation SCENARIO|core-control|sync-control|corrupt-readback");
    const std::string gpu=argv[1],mode=argv[2],scenario=argv[3];require(!gpu.empty()&&(mode=="plain"||mode=="validation"),"Invalid arguments");bool validation=mode=="validation";
    bool control=scenario=="core-control"||scenario=="sync-control",corrupt=scenario=="corrupt-readback";auto s=control?Shape{}:shape(corrupt?"odd":scenario);
    Context c(validation,gpu);auto identity=c.identity();std::uint64_t verified{};std::vector<std::string> hashes;std::string diagnostic;
    if(control)diagnostic=validationControl(c,scenario);
    else{
        Upload upload(c,s);std::vector<std::byte> expected(std::size_t(s.w)*s.h*s.bpp);
        for(unsigned i=1;i<=4;++i){payload(expected,s,i);upload.write(expected);auto actual=upload.read(expected.size());if(corrupt)actual.back()^=std::byte{1};require(actual==expected,"Full changing upload readback mismatch");auto hash=sha256(actual);require(hash==sha256(expected),"Upload SHA256 mismatch");hashes.push_back(hash);verified+=actual.size();}
        upload.close();
    }
    auto runtime=modules();c.close();if(!control)require(errors==0,"Native validation error delivered");
    std::cout<<"{\"schema_version\":1,\"task\":\"P3-LBEN-01-LLGL-readiness\",\"sdk\":\"llgl\",\"api\":\"vulkan\",\"configuration\":\"Release\",\"contract\":\"vulkan_upload_cpu_payload_to_texture_fence_complete_each_sample_readback_v1\",\"source_fingerprint\":"<<quote(GFX_LLGL_UPLOAD_FINGERPRINT)<<",\"LLGL_commit\":"<<quote(GFX_LLGL_PIN)<<','<<identity
      <<",\"dependency_profile\":"<<quote(GFX_LLGL_PROFILE)<<",\"validation\":"<<(validation?"true":"false")<<",\"scenario\":"<<quote(scenario)<<",\"control\":"<<(control?"true":"false")<<",\"control_diagnostic\":"<<quote(diagnostic)<<",\"submitted_control_work\":0,\"delivered_errors\":"<<errors
      <<",\"width\":"<<s.w<<",\"height\":"<<s.h<<",\"texel_bytes\":"<<s.bpp<<",\"warmup\":"<<(control?0:1)<<",\"sample_count\":"<<(control?0:3)<<",\"verified_images\":"<<hashes.size()<<",\"verified_bytes\":"<<verified<<",\"readback_verified\":"<<(control?"false":"true")<<",\"row_stride\":0,\"layer_stride\":0,\"payload_sha256\":[";
    for(unsigned i=0;i<hashes.size();++i){if(i)std::cout<<',';std::cout<<quote(hashes[i]);}
    std::cout<<"],\"runtime_modules\":"<<runtime<<",\"performance_samples\":0,\"samples_ms\":[],\"gpu_ms\":null,\"performance_accepted\":false,\"native_workload_shortcuts\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
