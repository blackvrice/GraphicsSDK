#include "BgraSupport.hpp"
#include <cstring>
#include <iomanip>
static_assert(static_cast<int>(gfx::Format::RGBA8UNorm)==0&&static_cast<int>(gfx::Format::RGBA16Float)==1&&static_cast<int>(gfx::Format::D32Float)==2&&static_cast<int>(gfx::Format::BGRA8UNorm)==3);
int main(int argc,char** argv)try{
    auto backend=gfx::Backend::Vulkan;bool validation{},warp{};
    for(int i=1;i<argc;++i)if(!std::strcmp(argv[i],"--d3d12"))backend=gfx::Backend::D3D12;else if(!std::strcmp(argv[i],"--validation"))validation=true;else if(!std::strcmp(argv[i],"--warp"))warp=true;else throw std::runtime_error("Unexpected BGRA argument");
    auto adapters=bgraTest::take(gfx::enumerateAdapters(backend));unsigned count{};
    if(backend==gfx::Backend::D3D12&&!warp&&!std::any_of(adapters.begin(),adapters.end(),[](auto& a){return !a.softwareAdapter;})){std::cout<<"SKIP no hardware D3D12 BGRA adapter\n";return 77;}
    for(auto& a:adapters){if(backend==gfx::Backend::D3D12&&a.softwareAdapter!=warp)continue;gfx::DeviceDesc desc;desc.backend=backend;desc.validation=validation;desc.softwareAdapter=a.softwareAdapter;desc.adapter=a.id;
        auto d=bgraTest::take(gfx::createDevice(desc));bgraTest::offscreen(*d);bgraTest::empty(*d);++count;
    }
    bgraTest::check(count>0,"Actual adapter required");std::cout<<"{\"event\":\"bgra\",\"source\":"<<std::quoted(gfx::buildFingerprint())<<",\"adapters\":"<<count<<",\"validation\":"<<std::boolalpha<<validation<<",\"images\":"<<bgraTest::images<<",\"transfers\":"<<bgraTest::transfers<<",\"pixel_bytes\":"<<bgraTest::pixelBytes<<",\"transfer_bytes\":"<<bgraTest::transferBytes<<",\"padding_rows\":"<<bgraTest::paddedRows<<",\"errors\":0}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
