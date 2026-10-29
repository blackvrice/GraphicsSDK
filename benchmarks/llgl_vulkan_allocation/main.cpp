#include "../llgl_vulkan_pso/Support.hpp"
#include "../vulkan_allocation/Payload.hpp"
#include <set>
#include <cstring>

#ifndef NDEBUG
#error Release required
#endif
namespace llglallocation {
using namespace llglpso;
struct Totals {std::uint64_t created{},released{},used{},verified{},nativeChecked{},bytes{};};
template<class T>std::uint64_t bits(T handle){std::uint64_t out{};static_assert(sizeof(handle)==sizeof(out));std::memcpy(&out,&handle,sizeof(out));return out;}
struct Allocation {
    Context& c;allocbench::Work w;Totals totals;std::vector<LLGL::Resource*> resources;std::vector<LLGL::Buffer*> uploads,downloads;LLGL::CommandBuffer* command{};LLGL::Fence* fence{};
    Allocation(Context& context,allocbench::Work work):c(context),w(work){}
    void create(const std::string& control){
        require(resources.empty(),"Previous resources retained");resources.resize(w.count);std::set<LLGL::Resource*> unique;std::set<std::uint64_t> native;
        LLGL::BufferDescriptor bd{};bd.size=allocbench::bufferBytes;bd.bindFlags=LLGL::BindFlags::VertexBuffer|LLGL::BindFlags::CopySrc|LLGL::BindFlags::CopyDst;
        LLGL::TextureDescriptor td{};td.extent={allocbench::width,allocbench::height,1};td.format=LLGL::Format::RGBA8UNorm;td.mipLevels=1;td.arrayLayers=1;td.samples=1;td.bindFlags=LLGL::BindFlags::Sampled|LLGL::BindFlags::CopySrc|LLGL::BindFlags::CopyDst;
        for(unsigned i=0;i<w.count;++i){
            if(control=="missing-resource"&&i+1==w.count)continue;
            if(control=="alias-resource"&&i+1==w.count)resources[i]=resources[0];
            else{resources[i]=w.texture?static_cast<LLGL::Resource*>(c.renderer->CreateTexture(td)):static_cast<LLGL::Resource*>(c.renderer->CreateBuffer(bd));++totals.created;}
            require(resources[i]!=nullptr,"Device resource create failed");require(unique.insert(resources[i]).second,"Duplicate live public resource handle");
            LLGL::Vulkan::ResourceNativeHandle n{};require(resources[i]->GetNativeHandle(&n,sizeof(n)),"Native resource identity absent");std::uint64_t handle{};
            if(w.texture){require(n.type==LLGL::Vulkan::ResourceNativeType::Image&&n.image.image&&n.image.extent.width==allocbench::width&&n.image.extent.height==allocbench::height&&n.image.extent.depth==1&&n.image.format==VK_FORMAT_R8G8B8A8_UNORM&&n.image.numMipLevels==1&&n.image.numArrayLayers==1&&n.image.sampleCountBits==VK_SAMPLE_COUNT_1_BIT,"Native texture shape/type differs");auto required=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;require((n.image.imageUsageFlags&required)==required,"Native sampled/copy usage absent");handle=bits(n.image.image);}
            else{require(n.type==LLGL::Vulkan::ResourceNativeType::Buffer&&n.buffer.buffer,"Native buffer type differs");handle=bits(n.buffer.buffer);}
            require(native.insert(handle).second,"Duplicate live native resource handle");++totals.nativeChecked;
        }
        require(unique.size()==w.count&&native.size()==w.count,"Public resource creation count differs");
    }
    void complete(){auto& q=*c.renderer->GetCommandQueue();q.Submit(*command);q.Submit(*fence);require(q.WaitFence(*fence,5'000'000'000ull),"Resource copy fence timeout");q.WaitIdle();}
    std::vector<std::string> use(unsigned iteration,const std::string& control){
        const auto size=allocbench::bytes(w);uploads.resize(w.count);downloads.resize(w.count);
        LLGL::BufferDescriptor b{};b.size=size;b.bindFlags=LLGL::BindFlags::CopySrc;b.cpuAccessFlags=LLGL::CPUAccessFlags::Write;
        for(unsigned i=0;i<w.count;++i){uploads[i]=c.renderer->CreateBuffer(b);require(uploads[i]!=nullptr,"Upload staging create failed");auto data=allocbench::payload(w,control=="stale-iteration"?0:iteration,control=="wrong-resource-payload"&&i+1==w.count?i+1:i);auto p=c.renderer->MapBuffer(*uploads[i],LLGL::CPUAccess::WriteOnly);require(p!=nullptr,"Upload map failed");std::memcpy(p,data.data(),size);c.renderer->UnmapBuffer(*uploads[i]);}
        b.bindFlags=LLGL::BindFlags::CopyDst;b.cpuAccessFlags=LLGL::CPUAccessFlags::Read;for(auto& p:downloads){p=c.renderer->CreateBuffer(b);require(p!=nullptr,"Readback staging create failed");}
        command=c.renderer->CreateCommandBuffer({});fence=c.renderer->CreateFence();require(command&&fence,"Resource copy command create failed");LLGL::TextureRegion region{};region.extent={allocbench::width,allocbench::height,1};
        // This fixed backend has no transfer barrier in public CopyBuffer.
        // Separate completed public submissions keep workload copies native-API-free.
        command->Begin();for(unsigned i=0;i<w.count;++i){if(w.texture)command->CopyTextureFromBuffer(*static_cast<LLGL::Texture*>(resources[i]),region,*uploads[i],0,0,0);else command->CopyBuffer(*static_cast<LLGL::Buffer*>(resources[i]),0,*uploads[i],0,size);}command->End();complete();
        command->Begin();for(unsigned i=0;i<w.count;++i){if(control=="skip-copy"&&i+1==w.count)continue;if(w.texture)command->CopyBufferFromTexture(*downloads[i],0,*static_cast<LLGL::Texture*>(resources[i]),region,0,0);else command->CopyBuffer(*downloads[i],0,*static_cast<LLGL::Buffer*>(resources[i]),0,size);++totals.used;}command->End();complete();
        c.renderer->Release(*command);command=nullptr;c.renderer->Release(*fence);fence=nullptr;
        std::vector<std::string> hashes;for(unsigned i=0;i<w.count;++i){std::vector<std::byte> data(size);c.renderer->ReadBuffer(*downloads[i],0,data.data(),size);if(control=="corrupt-readback"&&i+1==w.count)data.back()^=std::byte{1};if(control=="short-readback"&&i+1==w.count)data.pop_back();require(data.size()==size,"Incomplete full resource readback");require(data==allocbench::payload(w,iteration,i),"Full changing resource readback mismatch");hashes.push_back(sha256(data));++totals.verified;totals.bytes+=data.size();}
        for(auto p:uploads)c.renderer->Release(*p);uploads.clear();for(auto p:downloads)c.renderer->Release(*p);downloads.clear();return hashes;
    }
    void release(const std::string& control){unsigned count{};for(unsigned i=0;i<resources.size();++i){if(control=="skip-release"&&i+1==w.count)continue;releaseOne(resources[i]);resources[i]=nullptr;++count;++totals.released;}require(count==w.count,"Public resource release count differs");resources.clear();}
    void releaseOne(LLGL::Resource* p){if(!p)return;if(w.texture)c.renderer->Release(*static_cast<LLGL::Texture*>(p));else c.renderer->Release(*static_cast<LLGL::Buffer*>(p));}
    void close(){c.renderer->GetCommandQueue()->WaitIdle();if(command)c.renderer->Release(*command);command=nullptr;if(fence)c.renderer->Release(*fence);fence=nullptr;for(auto p:uploads)if(p)c.renderer->Release(*p);uploads.clear();for(auto p:downloads)if(p)c.renderer->Release(*p);downloads.clear();std::set<LLGL::Resource*> unique;for(auto p:resources)if(p&&unique.insert(p).second)releaseOne(p);resources.clear();}
    ~Allocation(){close();}
};
}
int main(int argc,char** argv)try{
    using namespace llglallocation;require(argc==4,"Usage: llgl_vulkan_allocation EXPECTED_GPU plain|validation SCENARIO|CONTROL");std::string gpu=argv[1],mode=argv[2],name=argv[3];require(!gpu.empty()&&(mode=="plain"||mode=="validation"),"Invalid arguments");bool validation=mode=="validation",diagnostic=name=="core-control"||name=="sync-control";
    bool custom=name=="missing-resource"||name=="alias-resource"||name=="skip-release"||name=="wrong-resource-payload"||name=="stale-iteration"||name=="skip-copy"||name=="corrupt-readback"||name=="short-readback";auto w=diagnostic?allocbench::Work{}:custom?allocbench::Work{name=="stale-iteration"||name=="corrupt-readback"||name=="short-readback",2}:allocbench::work(name);
    Context c(validation,gpu);Observer observer;auto deviceIdentity=identity(c);std::string token;Totals totals;std::vector<std::vector<std::string>> hashes;
    if(diagnostic)token=validationControl(c,name);else{Allocation adapter(c,w);for(unsigned i=0;i<4;++i){adapter.create(name);hashes.push_back(adapter.use(i,name));adapter.release(name);}totals=adapter.totals;adapter.close();auto expected=std::uint64_t(w.count)*4;require(totals.created==expected&&totals.released==expected&&totals.used==expected&&totals.verified==expected&&totals.nativeChecked==expected,"Public resource lifecycle counters differ");}
    auto observed=observer.snapshot();auto runtime=modules();c.close();auto retired=observer.snapshot();require(retired.liveInstances==0&&retired.liveDevices==0,"Observer observed live native objects after teardown");if(!diagnostic)require(errors==0,"Native validation error delivered");
    std::cout<<"{\"schema_version\":1,\"task\":\"P3-CMP-06\",\"sdk\":\"llgl\",\"api\":\"vulkan\",\"configuration\":\"Release\",\"profile_id\":\"llgl-fixed-pin-transfer-src-v1\",\"source_fingerprint\":"<<quote(GFX_LLGL_ALLOCATION_FINGERPRINT)<<','<<deviceIdentity
      <<",\"contract\":\"vulkan_resource_create_public_release_full_readback_v1\",\"category\":\"cpu_api\",\"scenario\":"<<quote(name)<<",\"validation\":"<<(validation?"true":"false")<<",\"control\":"<<(diagnostic?"true":"false")<<",\"control_diagnostic\":"<<quote(token)<<",\"delivered_errors\":"<<errors<<",\"submitted_control_work\":0"
      <<",\"resource_kind\":"<<quote(diagnostic?"none":w.texture?"texture":"buffer")<<",\"width\":"<<(w.texture?65:0)<<",\"height\":"<<(w.texture?37:0)<<",\"texel_bytes\":"<<(w.texture?4:0)<<",\"buffer_bytes\":"<<(!diagnostic&&!w.texture?4096:0)<<",\"resources_per_iteration\":"<<w.count<<",\"created_resources\":"<<totals.created<<",\"released_resource_handles\":"<<totals.released<<",\"used_resources\":"<<totals.used<<",\"verified_resources\":"<<totals.verified<<",\"unique_native_resources_checked\":"<<totals.nativeChecked<<",\"native_resource_handles_verified\":"<<(diagnostic?"false":"true")<<",\"verified_images\":"<<(w.texture?totals.verified:0)<<",\"verified_buffers\":"<<(!w.texture?totals.verified:0)<<",\"verified_bytes\":"<<totals.bytes
      <<",\"allocation_policy\":\"public_device_resource_default_policy\",\"usage_policy\":"<<quote(diagnostic?"none":w.texture?"sampled_rgba8_2d":"llgl_vertex_buffer_copy_src_dst")<<",\"buffer_cpu_access_flags\":0,\"initial_data_supplied\":false,\"copy_submission_policy\":\"separate_upload_download_completed_fence_idle\",\"staging_in_create_release_boundary\":false,\"row_stride\":0,\"layer_stride\":0,\"GPU_use_in_timing\":false,\"GPU_memory_reclamation_verified\":false,\"native_allocation_count_verified\":false,\"allocator_cache_state_known\":false,\"warmup\":"<<(diagnostic?0:1)<<",\"sample_count\":"<<(diagnostic?0:3)<<",\"readback_verified\":"<<(diagnostic?"false":"true")<<",\"payload_sha256\":[";
    for(unsigned i=0;i<hashes.size();++i){if(i)std::cout<<',';std::cout<<'[';for(unsigned j=0;j<hashes[i].size();++j){if(j)std::cout<<',';std::cout<<quote(hashes[i][j]);}std::cout<<']';}
    std::cout<<"],\"native_draw_calls\":"<<observed.calls<<",\"observer_path\":"<<quote(observer.path)<<",\"observed_instance_api_version\":"<<observed.instanceApi<<",\"observer_live_instances_after_close\":"<<retired.liveInstances<<",\"observer_live_devices_after_close\":"<<retired.liveDevices<<",\"runtime_modules\":"<<runtime<<",\"performance_samples\":0,\"samples_ms\":[],\"gpu_ms\":null,\"performance_accepted\":false,\"native_workload_shortcuts\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
