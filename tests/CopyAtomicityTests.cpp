#include <graphics/Graphics.hpp>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using S=gfx::ResourceState;
unsigned assertions{},rejections{},cases{},images{};
std::uint64_t pixels{},verifiedBytes{};
void check(bool v,const char* text){++assertions;if(!v)throw std::runtime_error(text);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
template<class T>void reject(gfx::Result<T> r,gfx::ErrorCode code,const char* text){check(!r&&r.error().code==code,text);++rejections;}
auto command(gfx::Device& d){auto c=take(d.createCommandList());take(c->begin());return c;}
void finish(gfx::Device& d,gfx::CommandList& c){take(c.end());take(d.queue().wait(take(d.queue().submit(c))));}
void resources(gfx::Device& d,unsigned expected){const auto s=take(d.resourceStatistics());check(s.uniqueResources==expected&&s.resourceAllocationBytes==std::uint64_t(expected)*65536&&s.pendingSubmissions==0&&s.pendingObjectReferences==0,"Exactly the accepted native resource set is retained");}
void rejected(gfx::Device& d,unsigned kind){
    constexpr unsigned size=8192;
    auto upload=take(d.createBuffer({size,gfx::Memory::Upload})),device=take(d.createBuffer({size})),readback=take(d.createBuffer({size,gfx::Memory::Readback}));auto texture=take(d.createTexture({63,7}));
    if(kind==7){auto warm=command(d);take(warm->transition(texture,S::CopySource));finish(d,*warm);}
    auto c=command(d);const std::array<std::byte,size> data{};
    for(unsigned i=0;i<64;++i){
        gfx::Result<void> r;
        switch(kind){
        case 0:r=c->transition(upload,S::Common);break;
        case 1:r=c->transition(readback,S::CopySource);break;
        case 2:r=c->copyBuffer(device,readback,size);break;
        case 3:r=c->copyBuffer(upload,device,size);break;
        case 4:r=c->copyBufferToTexture(device,texture);break;
        case 5:r=c->copyBufferToTexture(upload,texture);break;
        case 6:r=c->copyTextureToBuffer(texture,readback);break;
        case 7:r=c->copyTextureToBuffer(texture,device);break;
        }
        reject(std::move(r),kind<2?gfx::ErrorCode::InvalidArgument:gfx::ErrorCode::InvalidState,"Reject invalid transition/copy state");
        if(kind==0||kind==3||kind==5)take(upload->write(0,data));
    }
    // An independent accepted transition changes the rejected source's version.
    if(kind==7){auto change=command(d);take(change->transition(texture,S::Common));finish(d,*change);}
    upload.reset();device.reset();readback.reset();texture.reset();
    resources(d,1);finish(d,*c);c.reset();resources(d,0);++cases;
    std::cout<<"{\"event\":\"copy_rejection_case\",\"adapter_id\":"<<d.caps().adapterId.value<<",\"case\":"<<kind<<",\"repetitions\":64,\"resources_after_drop\":1,\"submit_passed\":true}\n";
}
void buffer(gfx::Device& d){
    constexpr unsigned size=4096;const std::array<std::byte,size> data=[] {std::array<std::byte,size> v{};for(unsigned i=0;i<size;++i)v[i]=std::byte(i%251);return v;}();
    auto upload=take(d.createBuffer({size,gfx::Memory::Upload})),device=take(d.createBuffer({size})),readback=take(d.createBuffer({size,gfx::Memory::Readback})),unused=take(d.createBuffer({size}));take(upload->write(0,data));
    auto c=command(d);take(c->transition(device,S::CopyDestination));take(c->copyBuffer(upload,device,size));take(c->transition(device,S::CopySource));
    reject(c->copyBuffer(device,unused,size),gfx::ErrorCode::InvalidState,"Rejected destination does not join an accepted source's use set");unused.reset();
    take(c->copyBuffer(device,readback,size));upload.reset();device.reset();resources(d,4);finish(d,*c);
    check(take(readback->read(0,size))==std::vector<std::byte>(data.begin(),data.end()),"Accepted upload/device/readback survives wrapper release");verifiedBytes+=size;
    c.reset();readback.reset();resources(d,0);
}
void shadow(gfx::Device& d){
    auto upload=take(d.createBuffer({256,gfx::Memory::Upload})),readback=take(d.createBuffer({256,gfx::Memory::Readback}));auto c=command(d);
    take(c->transition(upload,S::ConstantBuffer));
    reject(c->copyBuffer(upload,readback,256),gfx::ErrorCode::InvalidState,"Validate against the accepted recorded state rather than global CopySource");
    const std::array<std::byte,256> data{};take(upload->write(0,data));readback.reset();upload.reset();resources(d,2);take(c->end());
    const auto timeline=take(d.queue().completedValue());reject(d.queue().submit(*c),gfx::ErrorCode::InvalidState,"Accepted use still detects a later upload write");check(take(d.queue().completedValue())==timeline,"Rejected stale submit does not advance the fence");
    c.reset();resources(d,0);
}
void texture(gfx::Device& d,gfx::Format format){
    auto image=take(d.createTexture({63,7,format}));const auto layout=image->footprint();const auto rowBytes=63u*(format==gfx::Format::RGBA8UNorm?4u:8u);
    std::vector<std::byte> data(layout.size,std::byte{0xee});for(unsigned y=0;y<7;++y)for(unsigned x=0;x<rowBytes;++x)data[y*layout.rowPitch+x]=std::byte((x+y*13)%251);
    auto upload=take(d.createBuffer({layout.size,gfx::Memory::Upload})),readback=take(d.createBuffer({layout.size,gfx::Memory::Readback})),unused=take(d.createBuffer({layout.size})),unusedUpload=take(d.createBuffer({layout.size,gfx::Memory::Upload}));take(upload->write(0,data));
    auto c=command(d);take(c->transition(image,S::CopyDestination));take(c->copyBufferToTexture(upload,image));take(c->transition(image,S::CopySource));
    reject(c->copyTextureToBuffer(image,unused),gfx::ErrorCode::InvalidState,"Rejected texture readback destination remains unretained");
    reject(c->copyBufferToTexture(unusedUpload,image),gfx::ErrorCode::InvalidState,"Texture upload checks its recorded CopySource state");take(unusedUpload->write(0,data));unused.reset();unusedUpload.reset();
    take(c->copyTextureToBuffer(image,readback));upload.reset();image.reset();resources(d,4);finish(d,*c);
    const auto result=take(readback->read(0,layout.size));for(unsigned y=0;y<7;++y)for(unsigned x=0;x<rowBytes;++x)check(result[y*layout.rowPitch+x]==data[y*layout.rowPitch+x],"All texture payload bytes match including after validation rejection");
    ++images;pixels+=63*7;verifiedBytes+=std::uint64_t(rowBytes)*7;c.reset();readback.reset();resources(d,0);
}
}
int main(int argc,char** argv)try{
    check(argc==2,"Expected --gpu or --validation");const std::string_view mode=argv[1];check(mode=="--gpu"||mode=="--validation","Unknown mode");const bool validation=mode=="--validation";
    const auto adapters=take(gfx::enumerateAdapters());check(!adapters.empty(),"Real adapters required");
    for(const auto& adapter:adapters){auto d=take(gfx::createDevice({gfx::Backend::D3D12,validation,adapter.softwareAdapter,false,adapter.id}));for(unsigned i=0;i<8;++i)rejected(*d,i);buffer(*d);shadow(*d);texture(*d,gfx::Format::RGBA8UNorm);texture(*d,gfx::Format::RGBA16Float);if(validation)check(take(d->validationErrorCount())==0,"No native validation errors from accepted/rejected operations");}
    std::cout<<"{\"event\":\"copy_atomicity_result\",\"sdk_sha256\":\""<<gfx::buildFingerprint()<<"\",\"adapters\":"<<adapters.size()<<",\"validation\":"<<(validation?"true":"false")<<",\"cases\":"<<cases<<",\"full_images\":"<<images<<",\"verified_pixels\":"<<pixels<<",\"verified_bytes\":"<<verifiedBytes<<",\"rejections\":"<<rejections<<",\"passed\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
