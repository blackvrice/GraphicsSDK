#include "FaultPlan.hpp"
#include <iostream>
#include <stdexcept>
int main()try{
    unsigned checks{};auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("Fault plan contract");};
    for(int a:{0,-1,-2,-13})for(int b:{0,-1,-2,-13})for(int bind:{0,-1,-2})for(unsigned poison:{0u,1u}){
        fault::Plan p;bool valid=!b||a;check(p.arm(a,b,bind,poison)==valid);
        if(!valid){check(!p.pending());continue;}
        check(p.pending()==unsigned(a!=0)+unsigned(b!=0)+unsigned(bind!=0));check(p.poison==(poison!=0));
        if(p.pending())check(!p.arm(0,0,0,0));
        check(p.allocate()==a);check(p.allocate()==b);check(!p.allocate());check(p.bind()==bind);check(!p.bind());check(!p.pending());check(p.arm(0,0,0,0));
    }
    for(int c:{1,-3,-4,INT32_MIN,INT32_MAX}){fault::Plan p;check(!p.arm(c,0,0,0));check(!p.arm(0,c,0,0));check(!p.arm(0,0,c,0));check(!p.pending());}
    fault::Plan p;check(!p.arm(-1,0,0,2));check(!p.pending());
    std::cout<<"PASS fault_plan_assertions="<<checks<<" GPU_calls=0\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
