#pragma once
#include <cstdint>
namespace loaderfault {
enum Kind : unsigned { Instance, Messenger, Device, Surface, Count };
inline constexpr const char* names[]{"instance","messenger","device","surface"};
struct Plan {
    unsigned kind{Count}; int result{}; bool poison{};
    bool arm(unsigned k,int r,unsigned p) {
        if(kind!=Count||k>=Count||(r!=-1&&r!=-2)||p>1||(p&&(k==Instance||k==Device)))return false;
        kind=k;result=r;poison=p!=0;return true;
    }
    int consume(unsigned k) {if(kind!=k)return 0;kind=Count;return result;}
    bool pending()const{return kind!=Count;}
};
struct Snapshot {
    std::uint64_t attempts[Count]{},created[Count]{},destroyed[Count]{},live[Count]{};
    std::uint64_t invalid{},errors{},injections{},pending{};
};
}
