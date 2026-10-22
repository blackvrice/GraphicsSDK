#pragma once
#include <graphics/Types.hpp>
#include <Windows.h>
#include <chrono>
#include <utility>

namespace gfx::detail {
// A single monotonic fence timeline, used only on the owning device thread.
// Keep at most one event registration whose threshold is not yet complete.
class FenceWaiter {
    HANDLE event{};
    std::uint64_t armedValue{};
public:
    FenceWaiter()=default;
    FenceWaiter(const FenceWaiter&)=delete;
    FenceWaiter& operator=(const FenceWaiter&)=delete;
    ~FenceWaiter(){reset();}
    void reset(){if(event)CloseHandle(event);event=nullptr;armedValue=0;}

    template<class FenceSource,class Poll,class Failure>
    Result<void> wait(FenceSource& fence,std::uint64_t value,std::chrono::milliseconds timeout,Poll&& poll,Failure&& failure){
        if(timeout.count()<0||timeout.count()>=INFINITE)
            return std::unexpected(Error{ErrorCode::InvalidArgument,"Invalid fence timeout"});
        auto done=poll();if(!done)return std::unexpected(done.error());
        if(*done>=value)return {};
        if(timeout.count()==0)return std::unexpected(Error{ErrorCode::Timeout,"GPU fence timeout"});
        const auto deadline=std::chrono::steady_clock::now()+timeout;
        if(!event){event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return failure(HRESULT_FROM_WIN32(GetLastError()),"CreateEvent failed");}
        while(true){
            if(!armedValue||*done>=armedValue){
                if(!ResetEvent(event))return failure(HRESULT_FROM_WIN32(GetLastError()),"Reset fence event failed");
                // Register the next incomplete point, not the caller's target.
                // A later wait for an earlier target can reuse this registration.
                const auto next=*done+1;
                const auto result=fence.SetEventOnCompletion(next,event);
                if(FAILED(result))return failure(result,"SetEventOnCompletion failed");
                armedValue=next;
            }
            const auto remaining=std::chrono::ceil<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
            const auto waited=WaitForSingleObject(event,remaining.count()>0?static_cast<DWORD>(remaining.count()):0);
            const auto error=waited==WAIT_FAILED?GetLastError():ERROR_SUCCESS;
            if(waited!=WAIT_OBJECT_0&&waited!=WAIT_TIMEOUT)
                return failure(HRESULT_FROM_WIN32(error?error:ERROR_GEN_FAILURE),"Fence wait failed");
            done=poll();if(!done)return std::unexpected(done.error());
            if(*done>=value)return {};
            if(waited==WAIT_TIMEOUT||std::chrono::steady_clock::now()>=deadline)
                return std::unexpected(Error{ErrorCode::Timeout,"GPU fence timeout"});
            // Old/completed registrations may signal late. Recheck the fence and
            // continue with the same deadline instead of treating a wake as completion.
        }
    }
};
}
