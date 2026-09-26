#pragma once
#include <algorithm>
#include <cmath>

namespace starlight::editor {
// Runtime-only resources: each Play session starts with fresh meters.
struct MovementResources {
    static constexpr double staminaMaximum=500, sprintDrain=15, staminaRecovery=25;
    static constexpr double dodgeRechargeSeconds=5;
    double stamina=staminaMaximum;
    int dodgeCharges=2;
    double dodgeRecharge=0;
    int grappleCharges=2;
    double grappleRecharge=0;
    bool exhausted=false;

    // Returns the fraction of this interval that can receive sprint speed.
    // Exhaustion locks sprint until full recovery, even if Shift stays held.
    double advance(double seconds,bool shiftHeld,bool moving){
        if(!std::isfinite(seconds) || seconds<=0)return 0;
        if(grappleCharges<2){
            grappleRecharge+=seconds;
            while(grappleCharges<2 && grappleRecharge>=10){grappleRecharge-=10;++grappleCharges;}
            if(grappleCharges==2)grappleRecharge=0;
        }
        if(dodgeCharges<2){
            dodgeRecharge+=seconds;
            while(dodgeCharges<2 && dodgeRecharge>=dodgeRechargeSeconds){
                dodgeRecharge-=dodgeRechargeSeconds;++dodgeCharges;
            }
            if(dodgeCharges==2)dodgeRecharge=0;
        }
        double remaining=seconds,sprinting=0;
        while(remaining>1e-9){
            if(exhausted){
                const double duration=std::min(remaining,(staminaMaximum-stamina)/staminaRecovery);
                stamina=std::min(staminaMaximum,stamina+duration*staminaRecovery);
                remaining-=duration;
                if(stamina<staminaMaximum-1e-9)break;
                stamina=staminaMaximum;exhausted=false;
                continue;
            }
            if(!shiftHeld){stamina=std::min(staminaMaximum,stamina+remaining*staminaRecovery);break;}
            if(!moving)break;
            const double duration=std::min(remaining,stamina/sprintDrain);
            stamina=std::max(0.,stamina-duration*sprintDrain);
            remaining-=duration;sprinting+=duration;
            if(stamina<=1e-9){stamina=0;exhausted=true;}
        }
        return sprinting/seconds;
    }
    bool spend_dodge(int count=1){
        if(count<1 || count>2 || dodgeCharges<count)return false;
        if(dodgeCharges==2)dodgeRecharge=0;
        dodgeCharges-=count;return true;
    }
    bool spend_grapple(){if(grappleCharges<=0)return false;if(grappleCharges==2)grappleRecharge=0;--grappleCharges;return true;}
};
}
