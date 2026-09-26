#pragma once
#include "render_scene.hpp"
#include "movement_resources.hpp"
#include <cmath>

namespace starlight::editor {
// Runtime traversal intent. The character controller resolves its velocity against geometry.
class GrappleMotion {
public:
    enum class State { Free, Pulling, Wall, Swing };
    State state=State::Free;
    Vec3 anchor{}, velocity{}, normal{0,0,1};
    double ropeLength=0,flightRemaining=0,flightDuration=0;
    Vec3 launchPoint{};
    bool flying()const{return flightRemaining>0;}
    Vec3 hook_position()const{return flying()?launchPoint+(anchor-launchPoint)*(1-flightRemaining/flightDuration):anchor;}
    Vec3 anchorVelocity{}, priorAnchorVelocity{};
    bool held=false;
    void detach(){state=State::Free;flightRemaining=0;}
    void reset(){*this=GrappleMotion{};}
    bool press(MovementResources& resources, bool valid, Vec3 player, Vec3 target, Vec3 surfaceNormal, bool overhead){
        if(held)return false;
        held=true;
        if(state!=State::Free || !resources.spend_grapple())return false;
        const double distance=(target-player).length();
        if(!valid || !std::isfinite(distance) || distance<.01 || distance>25)return false;
        anchor=target;normal=surfaceNormal.unit();ropeLength=distance;launchPoint=player;flightDuration=flightRemaining=distance/40.;
        state=overhead?State::Swing:State::Pulling;return true;
    }
    void release(){held=false;if(state==State::Pulling || state==State::Swing)detach();}
    void move_anchor(Vec3 next,double dt){
        if(!std::isfinite(dt) || dt<=0)return;
        const auto delta=next-anchor;
        if(!std::isfinite(delta.length()) || delta.length()>5){detach();return;}
        priorAnchorVelocity=anchorVelocity;anchorVelocity=delta*(1./dt);anchor=next;
    }
    Vec3 step(Vec3 player,Vec3 steering,double dt,double movementFactor=1){
        if(!std::isfinite(dt) || dt<=0 || state==State::Free)return velocity;
        dt=std::min(dt,1./30.);
        if(flying()){flightRemaining=std::max(0.,flightRemaining-dt);return velocity;}
        if(state==State::Pulling){
            auto delta=anchor+normal*.5-player;
            if(delta.length()<=.05){state=State::Wall;velocity={};}
            else velocity=delta.unit()*std::min(12.*movementFactor,delta.length()/dt);
        }else if(state==State::Wall){
            auto tangent=steering-normal*steering.dot(normal);
            velocity=(tangent.length()>1?tangent.unit()*(2*movementFactor):tangent*(2*movementFactor))+anchorVelocity;
        }else{
            velocity=velocity-priorAnchorVelocity;
            auto radial=(player-anchor).unit();
            auto tangent=steering-radial*steering.dot(radial);
            if(tangent.length()>1)tangent=tangent.unit();
            velocity=velocity+(Vec3{0,-9.81,0}+tangent*(6*movementFactor))*dt;
            if(velocity.length()>15)velocity=velocity.unit()*15;
            velocity=velocity+anchorVelocity;
            auto predicted=player+velocity*dt;
            auto offset=predicted-anchor;
            if(offset.length()>ropeLength){
                auto constrained=anchor+offset.unit()*ropeLength;
                velocity=(constrained-player)*(1./dt);
            }
        }
        return velocity;
    }
    Vec3 wall_jump(){
        if(state!=State::Wall)return velocity;
        velocity=normal*4+Vec3{0,5,0};detach();return velocity;
    }
};
}
