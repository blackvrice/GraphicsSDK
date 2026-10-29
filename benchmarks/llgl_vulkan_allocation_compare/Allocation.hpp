#pragma once
#include "../llgl_vulkan_pso/Support.hpp"
#include "../vulkan_allocation/Payload.hpp"
#include "../common/Adapter.hpp"
#include <set>
#include <cstring>
namespace llglallocation {
using namespace llglpso;
struct Totals {std::uint64_t created{},released{},used{},verified{},nativeChecked{},bytes{};};
template<class T>std::uint64_t bits(T handle){std::uint64_t out{};static_assert(sizeof(handle)==sizeof(out));std::memcpy(&out,&handle,sizeof(out));return out;}
struct Allocation {
    Context& c;allocbench::Work w;Totals totals;std::vector<LLGL::Resource*> resources;std::vector<LLGL::Buffer*> uploads,downloads;LLGL::CommandBuffer* command{};LLGL::Fence* fence{};
    Allocation(Context& context,allocbench::Work work):c(context),w(work){}
    double create(const std::string& control){
        require(resources.empty(),"Previous resources retained");resources.resize(w.count);
        LLGL::BufferDescriptor bd{};bd.size=allocbench::bufferBytes;bd.bindFlags=LLGL::BindFlags::VertexBuffer|LLGL::BindFlags::CopySrc|LLGL::BindFlags::CopyDst;
        LLGL::TextureDescriptor td{};td.extent={allocbench::width,allocbench::height,1};td.format=LLGL::Format::RGBA8UNorm;td.mipLevels=1;td.arrayLayers=1;td.samples=1;td.bindFlags=LLGL::BindFlags::Sampled|LLGL::BindFlags::CopySrc|LLGL::BindFlags::CopyDst;
        unsigned limit=w.count-((control=="missing-resource"||control=="alias-resource")?1:0);auto begin=bench::Clock::now();
        for(unsigned i=0;i<limit;++i){resources[i]=w.texture?static_cast<LLGL::Resource*>(c.renderer->CreateTexture(td)):static_cast<LLGL::Resource*>(c.renderer->CreateBuffer(bd));require(resources[i]!=nullptr,"Device resource create failed");}
        auto end=bench::Clock::now();totals.created+=limit;if(control=="alias-resource")resources.back()=resources.front();
        std::set<LLGL::Resource*> unique;std::set<std::uint64_t> native;
        for(unsigned i=0;i<w.count;++i){if(!resources[i])continue;
            require(resources[i]!=nullptr,"Device resource create failed");require(unique.insert(resources[i]).second,"Duplicate live public resource handle");
            LLGL::Vulkan::ResourceNativeHandle n{};require(resources[i]->GetNativeHandle(&n,sizeof(n)),"Native resource identity absent");std::uint64_t handle{};
            if(w.texture){require(n.type==LLGL::Vulkan::ResourceNativeType::Image&&n.image.image&&n.image.extent.width==allocbench::width&&n.image.extent.height==allocbench::height&&n.image.extent.depth==1&&n.image.format==VK_FORMAT_R8G8B8A8_UNORM&&n.image.numMipLevels==1&&n.image.numArrayLayers==1&&n.image.sampleCountBits==VK_SAMPLE_COUNT_1_BIT,"Native texture shape/type differs");auto required=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;require((n.image.imageUsageFlags&required)==required,"Native sampled/copy usage absent");handle=bits(n.image.image);}
            else{require(n.type==LLGL::Vulkan::ResourceNativeType::Buffer&&n.buffer.buffer,"Native buffer type differs");handle=bits(n.buffer.buffer);}
            require(native.insert(handle).second,"Duplicate live native resource handle");++totals.nativeChecked;
        }
        require(unique.size()==w.count&&native.size()==w.count,"Public resource creation count differs");return bench::ms(begin,end);
    }
    void complete(){auto& q=*c.renderer->GetCommandQueue();q.Submit(*command);q.Submit(*fence);require(q.WaitFence(*fence,5'000'000'000ull),"Resource copy fence timeout");q.WaitIdle();}
    std::vector<std::string> use(unsigned iteration,const std::string& control,bool collectHashes){
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
        std::vector<std::string> hashes;for(unsigned i=0;i<w.count;++i){std::vector<std::byte> data(size);c.renderer->ReadBuffer(*downloads[i],0,data.data(),size);if(control=="corrupt-readback"&&i+1==w.count)data.back()^=std::byte{1};if(control=="short-readback"&&i+1==w.count)data.pop_back();require(data.size()==size,"Incomplete full resource readback");require(data==allocbench::payload(w,iteration,i),"Full changing resource readback mismatch");if(collectHashes)hashes.push_back(sha256(data));++totals.verified;totals.bytes+=data.size();}
        for(auto p:uploads)c.renderer->Release(*p);uploads.clear();for(auto p:downloads)c.renderer->Release(*p);downloads.clear();return hashes;
    }
    double release(const std::string& control){
        require(resources.size()==w.count&&std::all_of(resources.begin(),resources.end(),[](auto p){return p!=nullptr;}),"Invalid live public resource handles");unsigned limit=w.count-(control=="skip-release"?1:0);
        auto begin=bench::Clock::now();for(unsigned i=0;i<limit;++i)releaseOne(resources[i]);auto end=bench::Clock::now();
        for(unsigned i=0;i<limit;++i)resources[i]=nullptr;totals.released+=limit;require(limit==w.count,"Public resource release count differs");resources.clear();return bench::ms(begin,end);
    }
    void releaseOne(LLGL::Resource* p){if(!p)return;if(w.texture)c.renderer->Release(*static_cast<LLGL::Texture*>(p));else c.renderer->Release(*static_cast<LLGL::Buffer*>(p));}
    void close(){c.renderer->GetCommandQueue()->WaitIdle();if(command)c.renderer->Release(*command);command=nullptr;if(fence)c.renderer->Release(*fence);fence=nullptr;for(auto p:uploads)if(p)c.renderer->Release(*p);uploads.clear();for(auto p:downloads)if(p)c.renderer->Release(*p);downloads.clear();std::set<LLGL::Resource*> unique;for(auto p:resources)if(p&&unique.insert(p).second)releaseOne(p);resources.clear();}
    ~Allocation(){close();}
};
}
