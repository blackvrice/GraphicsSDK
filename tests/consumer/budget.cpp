// Installed public SDK only: no Win32/DXGI headers or native handles.
#include <graphics/Graphics.hpp>
#include <iostream>
#include <stdexcept>

namespace {
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
unsigned snapshots{};
void query(gfx::Device& device,bool software){
    for(auto segment:{gfx::MemorySegment::Local,gfx::MemorySegment::NonLocal}){
        auto result=device.queryMemoryBudget(segment);
        if(!result){check(software&&result.error().code==gfx::ErrorCode::Unsupported,"Unavailable installed memory query must be explicit");continue;}
        check(result->adapterId==device.caps().adapterId&&result->segment==segment,"Installed memory query identity");++snapshots;
        std::cout<<"installed_budget_id="<<result->adapterId.value<<" segment="<<static_cast<int>(segment)<<" usage="<<result->usageBytes<<" budget="<<result->budgetBytes<<" reservation="<<result->reservationBytes<<" available_for_reservation="<<result->availableForReservationBytes<<'\n';
    }
}
}
int main(int argc,char** argv)try{
    const bool validation=argc==2&&std::string_view(argv[1])=="--validation";check(argc==1||validation,"Unknown installed budget mode");
    const auto entries=take(gfx::enumerateAdapters());check(!entries.empty(),"Installed adapter list required");
    for(const auto& entry:entries){
        auto device=take(gfx::createDevice({gfx::Backend::D3D12,validation,entry.softwareAdapter,true,entry.id}));query(*device,entry.softwareAdapter);
        auto invalid=device->queryMemoryBudget(static_cast<gfx::MemorySegment>(2));check(!invalid&&invalid.error().code==gfx::ErrorCode::InvalidArgument,"Installed invalid segment rejection");
        if(validation)check(take(device->validationErrorCount())==0,"Installed healthy validation");
        take(device->removeForTesting());const auto status=device->checkStatus();check(!status&&status.error().code==gfx::ErrorCode::DeviceLost,"Installed observed native loss");auto lost=device->queryMemoryBudget(gfx::MemorySegment::Local);check(!lost&&lost.error().code==gfx::ErrorCode::DeviceLost&&lost.error().nativeCode==status.error().nativeCode,"Installed lost budget reason");
        auto fresh=take(device->recover());check(fresh->caps().adapterId==entry.id,"Installed recovery keeps selected adapter");query(*fresh,entry.softwareAdapter);
        if(validation)check(take(fresh->validationErrorCount())==0,"Installed recovered validation");
    }
    std::cout<<"PASS installed budget consumer: "<<entries.size()<<" adapters, "<<snapshots<<" snapshots, same-ID recovery\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
