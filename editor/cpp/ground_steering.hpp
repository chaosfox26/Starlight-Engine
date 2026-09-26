#pragma once
#include "mesh.hpp"
namespace starlight::editor {
// Continuous velocity steering. Target changes never assign actor position.
struct GroundSteering {
    Vec3 velocity{};
    Vec3 step(Vec3 position,Vec3 target,double dt,double speed,double stopDistance){
        if(!std::isfinite(dt)||dt<=0||!std::isfinite(speed)||speed<=0)return {};
        dt=std::min(dt,.05);
        auto delta=target-position;delta.y=0;double distance=delta.length();
        constexpr double acceleration=8;
        double remaining=std::max(0.,distance-stopDistance);
        double desiredSpeed=std::min(speed,std::sqrt(2*acceleration*remaining));
        auto desired=delta.unit()*desiredSpeed;
        auto change=desired-velocity;double limit=acceleration*dt;
        velocity=velocity+(change.length()>limit?change.unit()*limit:change);
        auto displacement=velocity*dt;
        if(distance>0 && displacement.dot(delta.unit())>remaining){
            // Do not cross the desired attack spacing while braking.
            displacement=displacement-delta.unit()*(displacement.dot(delta.unit())-remaining);
            velocity=displacement*(1/dt);
        }
        return displacement;
    }
    void stop(){velocity={};}
};
}
