#include <graphics/Graphics.hpp>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
template<class T> T take(gfx::Result<T> r) { if (!r) throw std::runtime_error(r.error().message);return std::move(*r); }
void take(gfx::Result<void> r) { if (!r) throw std::runtime_error(r.error().message); }
void check(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
void image(gfx::Device& d,unsigned index) {
    auto texture=take(d.createTexture({13,7,gfx::Format::RGBA8UNorm,true}));const auto f=texture->footprint();
    auto buffer=take(d.createBuffer({f.size,gfx::Memory::Readback}));auto cmd=take(d.createCommandList());
    take(cmd->begin());take(cmd->transition(texture,gfx::ResourceState::RenderTarget));
    take(cmd->beginRenderPass(texture,{index%2?1.f:0.f,index%2?0.f:1.f,0,1}));take(cmd->endRenderPass());
    take(cmd->transition(texture,gfx::ResourceState::CopySource));take(cmd->copyTextureToBuffer(texture,buffer));take(cmd->end());
    take(d.queue().wait(take(d.queue().submit(*cmd))));const auto bytes=take(buffer->read(0,f.size));
    const std::array<unsigned,4> expected{index%2?255u:0u,index%2?0u:255u,0,255};
    for (unsigned y=0;y<7;++y) for(unsigned x=0;x<13;++x) for(unsigned c=0;c<4;++c)
        check(std::to_integer<unsigned>(bytes[y*f.rowPitch+x*4+c])==expected[c],"Installed selected adapter image mismatch");
}
}
int main(int argc,char** argv) try {
    const bool validation=argc==2 && std::string_view(argv[1])=="--validation";
    check(argc==1 || validation,"Unknown consumer mode");
    const auto adapters=take(gfx::enumerateAdapters());check(!adapters.empty(),"No installed-package adapters");
    for (const auto& a:adapters) {
        auto d=take(gfx::createDevice({gfx::Backend::D3D12,validation,a.softwareAdapter,true,a.id}));
        check(d->caps().adapterId==a.id,"Installed consumer selection mismatch");image(*d,0);
        if(validation) check(take(d->validationErrorCount())==0,"Installed initial validation errors");
        take(d->removeForTesting());auto fresh=take(d->recover());
        check(fresh->caps().adapterId==a.id,"Installed consumer recovery identity changed");image(*fresh,1);
        take(fresh->queue().waitIdle());
        if(validation) check(take(fresh->validationErrorCount())==0,"Installed recovered validation errors");
        std::cout<<"installed_adapter_id="<<a.id.value<<" software="<<a.softwareAdapter<<" two_images_exact_recovery_passed\n";
    }
    std::cout<<"PASS installed adapter consumer: "<<adapters.size()<<" adapters, "<<adapters.size()*2<<" full images\n";return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
