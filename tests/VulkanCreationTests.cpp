#include "Creation.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
enum class Result { Success=0,HostOOM=-1,DeviceOOM=-2,Unknown=-13 };
unsigned checks{};
void check(bool b){++checks;if(!b)throw std::runtime_error("Creation ownership contract");}
template<class Handle>void run(Handle good,Handle poison){
    for(auto error:{Result::HostOOM,Result::DeviceOOM,Result::Unknown}){
        unsigned calls{};Handle owner{};
        auto create=[&](unsigned argument,Handle*out){++calls;check(argument==73);*out=poison;return error;};
        check(gfx::vulkan::acquireOutput(owner,create,73u)==error);check(calls==1);check(owner==Handle{});
        owner=good;check(gfx::vulkan::acquireOutput(owner,create,73u)==error);check(owner==good);check(calls==2);
    }
    unsigned calls{};Handle owner{};
    auto create=[&](Handle*out){++calls;*out=good;return Result::Success;};
    check(gfx::vulkan::acquireOutput(owner,create)==Result::Success);check(calls==1&&owner==good);
    owner={};auto noWrite=[&](Handle*){++calls;return Result::HostOOM;};
    check(gfx::vulkan::acquireOutput(owner,noWrite)==Result::HostOOM);check(owner==Handle{}&&calls==2);
}
}
int main()try{
    for(std::uint64_t i=1;i<=128;++i)run(i,std::uint64_t(0xf11));
    int object{},fake{};run(&object,&fake);run(static_cast<void*>(&object),static_cast<void*>(&fake));
    std::cout<<"PASS creation_assertions="<<checks<<" GPU_calls=0\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
