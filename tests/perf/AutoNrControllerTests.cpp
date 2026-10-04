#include "veyra/engine/AutoNrController.h"
#include <iostream>
#include <limits>
int main() {
    using veyra::engine::AutoNrController;AutoNrController c;c.reset(0);
    if(c.observe(4999999,20,30,10))return 1;
    if(c.observe(5000000,20,30,10)!=1)return 1;
    if(c.observe(10000000,20,30,10))return 1; // dwell protects visible switching
    if(c.observe(35000000,20,30,10)!=2)return 1;
    if(c.observe(65000000,20,30,10)!=3)return 1;
    if(c.observe(95000000,20,30,10)!=4)return 1;
    if(c.observe(125000000,20,30,10))return 1; // floor remains honestly over budget
    if(c.observe(130000000,4,30,10)||c.observe(160000000,4,30,10))return 1; // four/minute cap
    if(c.observe(605000000,4,30,10)!=3)return 1;
    c.discardObservations(605000000);
    if(c.observe(610000000,4,30,10))return 1;
    if(c.observe(615000000,4,30,10))return 1;
    if(c.observe(645000000,4,30,10)!=2)return 1;
    c.reset(700000000);
    if(c.level()!=0||c.observe(705000000,20,7,10)||c.observe(710000000,std::nullopt,30,10)||
       c.observe(715000000,std::numeric_limits<double>::quiet_NaN(),30,10)||c.observe(720000000,20,30,0))return 1;
    if(c.observe(725000000,11,30,10)!=1)return 1;
    if(c.observe(755000000,7.5,30,10)||c.observe(785000000,7.5,30,10))return 1; // hysteresis band
    c.reset(800000000,2);
    if(c.level()!=2||c.observe(805000000,20,30,10)!=3)return 1; // a retained graph's current level survives parameter revision
    std::cout<<"AUTO_NR_CONTROLLER_PASS transportBudget=1 dwell=1 hysteresis=1 cap=1 floor=1 invalidTiming=1 reset=1"<<std::endl;
    return 0;
}
