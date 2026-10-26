#include "Policy.hpp"
#include <iostream>
#include <stdexcept>
void check(bool value){if(!value)throw std::runtime_error("XCB event policy contract");}
int main(){xcbPresentation::Policy p;auto now=presentation::FramePacer::Clock::now();check(!p.active()&&p.timeout(now)==-1);p.map();check(p.active());p.unmap();check(!p.active()&&p.timeout(now)==-1);p.map();p.iconic(true);check(!p.active());p.iconic(false);check(p.active());p.configure(0,7);check(!p.active());p.configure(17,9);check(p.active()&&p.dirty);p.close();p.map();check(!p.active()&&p.timeout(now)==-1);xcbPresentation::Policy dead;dead.expire();dead.map();check(!dead.active());std::cout<<"PASS native-independent XCB lifecycle policies\n";}
