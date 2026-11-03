#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using F=gfx::Format; using S=gfx::ResourceState; using L=gfx::LoadOp;
unsigned assertions{},images{},passes{};std::uint64_t verifiedBytes{};std::filesystem::path output;
void check(bool x,const char* text){++assertions;if(!x)throw std::runtime_error(text);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message+" native="+std::to_string(r.error().nativeCode));return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
template<class T>void reject(const gfx::Result<T>& r,gfx::ErrorCode e,const char* text){check(!r&&r.error().code==e,text);}
gfx::DeviceDesc desc(){gfx::DeviceDesc d;d.backend=gfx::Backend::Metal;return d;}
void empty(gfx::Device& d){take(d.queue().waitIdle());auto s=take(d.resourceStatistics());check(!s.trackedObjects&&!s.uniqueResources&&!s.resourceAllocationBytes&&!s.pendingSubmissions&&!s.pendingObjectReferences,"All texture/recording/submission references reclaimed");}
void finish(gfx::Device& d,gfx::CommandList& c){take(c.end());take(d.queue().wait(take(d.queue().submit(c))));}
void pass(gfx::CommandList& c,const gfx::RenderPassDesc& p){take(c.beginRenderPass(p));take(c.debugMarker("inside color/depth pass"));take(c.endRenderPass());++passes;}
unsigned texel(F f){return f==F::RGBA16Float?8u:4u;}
struct Back { gfx::BufferPtr buffer;gfx::TextureDesc desc;gfx::TextureFootprint f; };
Back stage(gfx::Device& d,gfx::CommandList& c,const gfx::TexturePtr& t){
    auto f=t->footprint();auto b=take(d.createBuffer({f.size,gfx::Memory::Readback}));auto u=take(d.createBuffer({f.size,gfx::Memory::Upload}));
    take(u->write(0,std::vector<std::byte>(static_cast<std::size_t>(f.size),std::byte{0xd3})));
    take(c.copyBuffer(u,b,f.size));take(c.transition(t,S::CopySource));take(c.copyTextureToBuffer(t,b));return {b,t->description(),f};
}
void save(const std::string& name,const std::vector<std::byte>& bytes){
    if(!output.empty()){std::ofstream f(output/(name+".bin"),std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));check(f.good(),"Save full native texel/row-padding payload");}
    ++images;verifiedBytes+=bytes.size();
}
std::vector<std::byte> pattern(gfx::TextureDesc d,gfx::TextureFootprint f){
    std::vector<std::byte> p(static_cast<std::size_t>(f.size),std::byte{0x71});const unsigned seed=17+unsigned(d.format)*11+d.width;
    constexpr std::array<std::uint16_t,7> half{0,0x3400,0x3800,0x3a00,0x3c00,0xc000,0x4400};
    for(unsigned y=0;y<d.height;++y)for(unsigned x=0;x<d.width;++x){auto base=std::size_t(y)*f.rowPitch+x*texel(d.format);
        if(d.format==F::D32Float){float v=float((x+3*y+seed)%17)/16;std::memcpy(p.data()+base,&v,4);}
        else if(d.format==F::RGBA16Float){for(unsigned c=0;c<4;++c){auto v=half[(x*3+y*5+c+seed)%7];std::memcpy(p.data()+base+c*2,&v,2);}}
        else for(unsigned c=0;c<4;++c){auto byte=x*4+c;p[base+c]=std::byte((byte*37+y*19+seed*53+byte/251)&255);}
    }return p;
}
void roundtrip(gfx::Device& d,F format,unsigned w,unsigned h){
    gfx::TextureDesc td{w,h,format,false,format==F::D32Float};auto first=take(d.createTexture(td)),second=take(d.createTexture(td));auto f=first->footprint();
    check(f.rowBytes==w*texel(format)&&f.rowPitch>=f.rowBytes&&f.rowPitch%256==0&&f.size==std::uint64_t(f.rowPitch)*h,"Complete padded texture footprint");
    auto expected=pattern(td,f);auto u=take(d.createBuffer({f.size,gfx::Memory::Upload}));take(u->write(0,expected));
    auto gpu=take(d.createBuffer({f.size}));auto c=take(d.createCommandList());take(c->begin());
    take(c->transition(first,S::CopyDestination));take(c->copyBufferToTexture(u,first));take(c->copyBufferToTexture(u,first));
    take(c->transition(first,S::CopySource));take(c->transition(gpu,S::CopyDestination));take(c->copyTextureToBuffer(first,gpu));
    take(c->transition(gpu,S::CopySource));take(c->transition(second,S::CopyDestination));take(c->copyBufferToTexture(gpu,second));
    auto back=stage(d,*c,second);std::weak_ptr<gfx::Texture> a=first,b=second;first.reset();second.reset();u.reset();gpu.reset();
    check(a.expired()&&b.expired(),"Recording retains native textures, never public wrappers");finish(d,*c);c.reset();
    auto bytes=take(back.buffer->read(0,f.size));
    for(unsigned y=0;y<h;++y){auto base=std::size_t(y)*f.rowPitch;
        check(std::equal(bytes.begin()+base,bytes.begin()+base+f.rowBytes,expected.begin()+base),"Every texel survives two textures and private staging");
        check(std::all_of(bytes.begin()+base+f.rowBytes,bytes.begin()+base+f.rowPitch,[](auto v){return v==std::byte{0xd3};}),"Every row including final padding preserved");}
    save("roundtrip-"+std::to_string(unsigned(format))+"-"+std::to_string(w)+"x"+std::to_string(h),bytes);back.buffer.reset();empty(d);
}
float half(std::uint16_t b){unsigned e=(b>>10)&31,f=b&1023;return std::ldexp(e?1.f+f/1024.f:f/1024.f,e?int(e)-15:-14)*(b&0x8000?-1.f:1.f);}
void verify(const std::string& name,const Back& b,gfx::Color color,float depth=0){
    auto bytes=take(b.buffer->read(0,b.f.size));std::array values{color.r,color.g,color.b,color.a};if(b.desc.format==F::BGRA8UNorm)std::swap(values[0],values[2]);
    for(unsigned y=0;y<b.desc.height;++y){auto base=std::size_t(y)*b.f.rowPitch;
        for(unsigned x=0;x<b.desc.width;++x){auto p=base+x*texel(b.desc.format);
            if(b.desc.format==F::D32Float){float v;std::memcpy(&v,bytes.data()+p,4);check(std::isfinite(v)&&std::abs(v-depth)<.000001f,"Every depth sample matches clear/load");}
            else for(unsigned c=0;c<4;++c){float v;if(b.desc.format==F::RGBA16Float){std::uint16_t bits;std::memcpy(&bits,bytes.data()+p+c*2,2);v=half(bits);}else v=std::to_integer<unsigned>(bytes[p+c])/255.f;
                check(std::isfinite(v)&&std::abs(v-values[c])<=(b.desc.format==F::RGBA16Float?.001f:1.01f/255),"Every color sample matches clear/load/HDR/swizzle");}}
        check(std::all_of(bytes.begin()+base+b.f.rowBytes,bytes.begin()+base+b.f.rowPitch,[](auto v){return v==std::byte{0xd3};}),"Clear/readback leaves row padding intact");}
    save(name,bytes);
}
void clears(gfx::Device& d,F format){
    auto t=take(d.createTexture({65,17,format,true}));auto c=take(d.createCommandList());
    gfx::Color initial{.25f,.5f,.75f,1},replacement=format==F::RGBA16Float?gfx::Color{-2,.5f,4,1}:gfx::Color{1,0,.5f,.25f};
    for(unsigned phase=0;phase<3;++phase){take(c->begin());take(c->transition(t,S::RenderTarget));
        if(phase==0){take(c->beginRenderPass(t,initial));take(c->endRenderPass());++passes;}
        pass(*c,{{{t,phase==2?L::Clear:L::Load,phase==2?replacement:initial}},{}});
        auto back=stage(d,*c,t);finish(d,*c);verify("clear-"+std::to_string(unsigned(format))+"-"+std::to_string(phase),back,phase==2?replacement:initial);
    }c.reset();t.reset();empty(d);
}
void depth(gfx::Device& d){
    auto t=take(d.createTexture({63,9,F::D32Float,false,true}));auto c=take(d.createCommandList());
    for(unsigned phase=0;phase<2;++phase){take(c->begin());take(c->transition(t,phase?S::DepthRead:S::DepthWrite));
        pass(*c,{{},{t,phase?L::Load:L::Clear,.25f,phase!=0}});auto back=stage(d,*c,t);finish(d,*c);verify("depth-"+std::to_string(phase),back,{},.25f);}
    c.reset();t.reset();empty(d);
}
void mrt(gfx::Device& d,unsigned count){
    constexpr std::array formats{F::RGBA8UNorm,F::BGRA8UNorm,F::RGBA16Float};gfx::RenderPassDesc p;std::vector<gfx::TexturePtr> textures;std::vector<gfx::Color> colors;
    for(unsigned i=0;i<count;++i){auto t=take(d.createTexture({65,17,formats[i%3],true}));gfx::Color color{float(i&1),float((i>>1)&1),float((i>>2)&1),1};textures.push_back(t);colors.push_back(color);p.colors.push_back({t,L::Clear,color});}
    auto z=take(d.createTexture({65,17,F::D32Float,false,true}));p.depth={z,L::Clear,.75f};auto c=take(d.createCommandList());take(c->begin());
    for(auto& t:textures)take(c->transition(t,S::RenderTarget));take(c->transition(z,S::DepthWrite));pass(*c,p);std::vector<Back> backs;
    for(auto& t:textures)backs.push_back(stage(d,*c,t));backs.push_back(stage(d,*c,z));
    p={};textures.clear();z.reset();finish(d,*c);c.reset();
    for(unsigned i=0;i<count;++i)verify("mrt-"+std::to_string(count)+"-"+std::to_string(i),backs[i],colors[i]);
    verify("mrt-"+std::to_string(count)+"-depth",backs.back(),{},.75f);backs.clear();empty(d);
}
void contracts(gfx::Device& d){
    auto other=take(gfx::createDevice(desc()));reject(d.createTexture({0,4}),gfx::ErrorCode::InvalidArgument,"Zero extent");
    reject(d.createTexture({16385,1}),gfx::ErrorCode::InvalidArgument,"Oversized extent");
    reject(d.createTexture({4,4,F::D32Float}),gfx::ErrorCode::InvalidArgument,"Depth usage required");
    reject(d.createTexture({4,4,static_cast<F>(99)}),gfx::ErrorCode::InvalidArgument,"Invalid format");
    auto t=take(d.createTexture({3,2,F::RGBA8UNorm,true})),foreign=take(other->createTexture({3,2,F::RGBA8UNorm,true}));
    auto f=t->footprint();auto u=take(d.createBuffer({f.size,gfx::Memory::Upload})),b=take(d.createBuffer({f.size,gfx::Memory::Readback})),small=take(d.createBuffer({1,gfx::Memory::Upload}));
    auto c=take(d.createCommandList());reject(c->beginRenderPass(t,{}),gfx::ErrorCode::InvalidState,"Pass before recording");take(c->begin());
    reject(c->endRenderPass(),gfx::ErrorCode::InvalidState,"No active pass");
    reject(c->transition(gfx::TexturePtr{},S::CopySource),gfx::ErrorCode::InvalidArgument,"Null texture");
    reject(c->transition(foreign,S::CopySource),gfx::ErrorCode::WrongDevice,"Foreign texture");
    reject(c->transition(t,S::Present),gfx::ErrorCode::Unsupported,"Presentation state unavailable");
    reject(c->transition(t,S::DepthWrite),gfx::ErrorCode::InvalidArgument,"Depth state requires depth texture");
    take(c->transition(t,S::CopySource));reject(c->copyTextureToBuffer(t,b),gfx::ErrorCode::InvalidState,"Uninitialized copy source");
    take(c->transition(t,S::RenderTarget));reject(c->beginRenderPass({{{t,L::Load}},{}}),gfx::ErrorCode::InvalidState,"Uninitialized Load");
    reject(c->beginRenderPass({{{t,L::Clear,{}},{t,L::Clear,{}}},{}}),gfx::ErrorCode::InvalidArgument,"Duplicate attachments");
    reject(c->beginRenderPass(t,{NAN,0,0,1}),gfx::ErrorCode::InvalidArgument,"Nonfinite clear");
    auto mismatch=take(d.createTexture({4,2,F::RGBA8UNorm,true}));reject(c->beginRenderPass({{{t,L::Clear,{}},{mismatch,L::Clear,{}}},{}}),gfx::ErrorCode::InvalidArgument,"Mismatched extent");
    take(c->beginRenderPass(t,{1,0,0,1}));reject(c->beginRenderPass(t,{}),gfx::ErrorCode::InvalidState,"Nested pass");
    reject(c->transition(t,S::CopySource),gfx::ErrorCode::InvalidState,"Transition inside pass");
    reject(c->copyTextureToBuffer(t,b),gfx::ErrorCode::InvalidState,"Copy inside pass");reject(c->end(),gfx::ErrorCode::InvalidState,"End with open pass");
    take(c->debugMarker("pass marker"));take(c->endRenderPass());
    take(c->transition(t,S::CopyDestination));reject(c->copyBufferToTexture(small,t),gfx::ErrorCode::InvalidArgument,"Undersized footprint");
    take(u->write(0,std::vector<std::byte>(static_cast<std::size_t>(f.size),std::byte{3})));take(c->copyBufferToTexture(u,t));
    take(c->transition(t,S::CopySource));take(c->copyTextureToBuffer(t,b));take(c->end());take(u->write(f.size,{}));
    reject(d.queue().submit(*c),gfx::ErrorCode::InvalidState,"CPU write invalidates texture upload observation");
    take(c->begin());take(c->transition(t,S::RenderTarget));take(c->beginRenderPass(t,{0,1,0,1}));take(c->endRenderPass());take(c->end());
    auto stale=take(d.createCommandList());take(stale->begin());take(stale->transition(t,S::CopySource));take(stale->end());auto fence=take(d.queue().submit(*c));
    reject(d.queue().submit(*stale),gfx::ErrorCode::InvalidState,"Sibling texture version stale");take(d.queue().wait(fence));
    auto z=take(d.createTexture({3,2,F::D32Float,false,true}));take(c->begin());take(c->transition(z,S::DepthRead));
    reject(c->beginRenderPass({{},{z,L::Clear,.5f,true}}),gfx::ErrorCode::InvalidArgument,"Read-only depth cannot clear");
    reject(c->beginRenderPass({{},{z,L::Load,.5f,true}}),gfx::ErrorCode::InvalidState,"Read-only Load requires initialization");
    bool thread{};std::thread worker([&]{auto r=c->transition(t,S::CopySource);thread=!r&&r.error().code==gfx::ErrorCode::InvalidState;});worker.join();check(thread,"Texture thread guard");
    c.reset();stale.reset();t.reset();foreign.reset();mismatch.reset();z.reset();u.reset();b.reset();small.reset();empty(d);
    // Failed multi-attachment passes must not retain newly mentioned allocations.
    t=take(d.createTexture({3,2,F::RGBA8UNorm,true}));c=take(d.createCommandList());take(c->begin());
    reject(c->beginRenderPass(t,{}),gfx::ErrorCode::InvalidState,"Rejected pass leaves no attachment use");
    reject(c->transition(t,S::DepthRead),gfx::ErrorCode::InvalidArgument,"Rejected transition captures nothing");
    u=take(d.createBuffer({t->footprint().size,gfx::Memory::Upload}));
    reject(c->copyBufferToTexture(u,t),gfx::ErrorCode::InvalidState,"Rejected texture copy captures neither argument");
    take(u->write(0,{}));u.reset();t.reset();auto stats=take(d.resourceStatistics());
    check(!stats.trackedObjects&&!stats.uniqueResources,"Failed attachment references released");take(c->end());fence=take(d.queue().submit(*c));c.reset();take(d.queue().wait(fence));empty(d);
    t=take(d.createTexture({3,2,F::RGBA8UNorm,true}));auto second=take(d.createTexture({3,2,F::RGBA8UNorm,true}));
    c=take(d.createCommandList());take(c->begin());take(c->transition(t,S::RenderTarget));
    reject(c->beginRenderPass({{{t,L::Clear,{}},{second,L::Clear,{}}},{}}),gfx::ErrorCode::InvalidState,"Later invalid attachment rejects whole pass");
    reject(c->beginRenderPass({{{t,L::Load}},{}}),gfx::ErrorCode::InvalidState,"Rejected Clear must not initialize an earlier attachment");
    second.reset();stats=take(d.resourceStatistics());check(stats.uniqueResources==1&&stats.trackedObjects==1,"Rejected MRT captures no new native references");
    take(c->beginRenderPass(t,{0,0,1,1}));take(c->endRenderPass());finish(d,*c);c.reset();t.reset();empty(d);
}
}
int main(int argc,char** argv)try{
    check(argc>=1&&argc<=3,"Optional fingerprint and fresh output directory");if(argc>=2)check(gfx::buildFingerprint()==argv[1],"Installed/source fingerprint");
    if(argc==3){output=argv[2];check(std::filesystem::create_directory(output),"Fresh output required");}
    auto d=take(gfx::createDevice(desc()));check(d->caps().depthAttachment&&d->caps().multipleRenderTargets&&d->caps().resourceBinding,"Graphics capabilities");
    constexpr std::array formats{F::RGBA8UNorm,F::RGBA16Float,F::D32Float,F::BGRA8UNorm};
    for(auto f:formats)for(auto size:std::array<std::array<unsigned,2>,4>{{{1,1},{3,2},{65,17},{129,7}}})roundtrip(*d,f,size[0],size[1]);
    for(auto f:{F::RGBA8UNorm,F::RGBA16Float,F::BGRA8UNorm})clears(*d,f);depth(*d);for(auto n:{2u,4u,8u})mrt(*d,n);contracts(*d);
    auto old=take(d->createTexture({3,2}));auto footprint=old->footprint();auto c=take(d->createCommandList());take(c->begin());take(c->transition(old,S::CopyDestination));d.reset();
    check(old->description().width==3&&old->footprint().size==footprint.size,"Retired texture keeps CPU descriptor/footprint");
    reject(c->transition(old,S::CopySource),gfx::ErrorCode::DeviceLost,"Retired texture native operation rejected");
    std::cout<<"{\"schema_version\":1,\"contract\":\"metal_texture_pass_v1\",\"SDK_source_fingerprint\":\""<<gfx::buildFingerprint()
        <<"\",\"images\":"<<images<<",\"passes\":"<<passes<<",\"verified_bytes\":"<<verifiedBytes<<",\"assertions\":"<<assertions
        <<",\"texture_transfer_executed\":true,\"clear_load_pass_executed\":true,\"draw_executed\":false,\"native_validation_accepted\":false,\"physical_GPU_accepted\":false,\"performance_accepted\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
