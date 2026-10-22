#include <graphics/Graphics.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
int main()try{
    auto d=take(gfx::createDevice({gfx::Backend::D3D12,false,true}));
    auto upload=take(d->createBuffer({256,gfx::Memory::Upload})),destination=take(d->createBuffer({256}));auto c=take(d->createCommandList());take(c->begin());
    auto rejected=c->copyBuffer(upload,destination,256);if(rejected||rejected.error().code!=gfx::ErrorCode::InvalidState)return 1;
    const std::array<std::byte,256> data{};take(upload->write(0,data));upload.reset();destination.reset();take(c->end());
    const auto retained=take(d->resourceStatistics()).uniqueResources;auto submitted=d->queue().submit(*c);if(submitted)take(d->queue().wait(*submitted));
    std::cout<<"{\"sdk\":\""<<gfx::buildFingerprint()<<"\",\"rejected_copy_poisoned_submit\":"<<!submitted<<",\"retained_resources_after_drop\":"<<retained<<"}\n";
    return submitted&&retained==1?0:2;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
