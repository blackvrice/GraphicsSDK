#pragma once
#include <array>
#include <cstdint>

namespace fault {
// Bounded, explicit, process-local injection. No real driver memory exhaustion.
class Plan {
    std::array<int,2> allocations{};
    unsigned next{};
    int binding{};
public:
    bool poison{};
    static bool allocationCode(int c){return c==0||c==-1||c==-2||c==-13;}
    unsigned pending()const{return unsigned(allocations[0]!=0)+unsigned(allocations[1]!=0)+unsigned(binding!=0);}
    bool arm(int a,int b,int bind,unsigned dirty){
        if(pending()||!allocationCode(a)||!allocationCode(b)||(b&&!a)||(bind!=0&&bind!=-1&&bind!=-2)||dirty>1)return false;
        allocations={a,b};next=0;binding=bind;poison=dirty!=0;return true;
    }
    int allocate(){if(next>=2)return 0;auto c=allocations[next];if(c){allocations[next++]=0;}return c;}
    int bind(){auto c=binding;binding=0;return c;}
};
}
