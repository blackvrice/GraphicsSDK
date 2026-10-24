#include "Contract.hpp"
#include <iostream>
#include <stdexcept>
int main()try {
    unsigned checks{};auto check=[&](bool x){++checks;if(!x)throw std::runtime_error("Plan contract");};
    using namespace loaderfault;
    for(unsigned k=0;k<Count;++k)for(int r:{-1,-2})for(unsigned poison=0;poison<2;++poison){
        Plan p;const bool valid=!poison||(k!=Instance&&k!=Device);
        check(p.arm(k,r,poison)==valid);check(p.pending()==valid);
        if(!valid)continue;
        check(!p.arm(k,r,poison));
        for(unsigned other=0;other<Count;++other)if(other!=k){check(!p.consume(other));check(p.pending());}
        check(p.consume(k)==r);check(!p.pending());check(!p.consume(k));check(p.arm(k,r,poison));
    }
    for(unsigned k:{unsigned(Count),99u})for(int r:{-1,-2,-13,0,1}){Plan p;check(!p.arm(k,r,0));check(!p.pending());}
    for(int r:{-13,0,1})for(unsigned k=0;k<Count;++k){Plan p;check(!p.arm(k,r,0));check(!p.pending());}
    for(unsigned k=0;k<Count;++k){Plan p;check(!p.arm(k,-1,2));check(!p.pending());}
    std::cout<<"PASS loader plan assertions="<<checks<<" GPU_calls=0\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
