#include <graphics/Graphics.hpp>
#include <iostream>
int main(){
    auto device=gfx::createDevice({gfx::Backend::D3D12,false,true});if(!device)return 1;
    auto c=(*device)->createCommandList();if(!c||!(*c)->begin()||!(*c)->end())return 1;
    auto fence=(*device)->queue().submit(**c);if(!fence||!(*device)->queue().wait(*fence))return 1;
    auto invalid=(*device)->queue().wait(*fence,std::chrono::milliseconds(-1));
    std::cout<<"completed_negative_timeout_accepted="<<bool(invalid)<<" sdk="<<gfx::buildFingerprint()<<'\n';
    return invalid?2:invalid.error().code==gfx::ErrorCode::InvalidArgument?0:1;
}
