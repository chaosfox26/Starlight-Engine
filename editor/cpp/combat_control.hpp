#pragma once
#include "basic_attack.hpp"
#include "ground_steering.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_set>
namespace starlight::editor {
enum class ControlEffect { Stun, Knockback, Pull, Launch, Knockdown, Root, Slow, Silence, Fear, Taunt, Sleep, Blind, Disorient, Tether };
inline double control_duration(ControlEffect e){
    switch(e){
    case ControlEffect::Stun:case ControlEffect::Fear:return 2;
    case ControlEffect::Slow:case ControlEffect::Tether:return 4;
    case ControlEffect::Sleep:return 5;
    case ControlEffect::Knockback:case ControlEffect::Pull:return .5;
    case ControlEffect::Launch:case ControlEffect::Knockdown:return 1;
    default:return 3;
    }
}
inline double control_break_strength(ControlEffect e){
    switch(e){
    case ControlEffect::Stun:case ControlEffect::Launch:return 250;
    case ControlEffect::Sleep:case ControlEffect::Fear:case ControlEffect::Knockdown:return 200;
    case ControlEffect::Knockback:case ControlEffect::Pull:case ControlEffect::Root:case ControlEffect::Disorient:return 150;
    case ControlEffect::Slow:case ControlEffect::Blind:return 75;
    default:return 100;
    }
}
struct ControlSource {std::uint64_t id=0;bool player=false;};
// Per-actor runtime state. The caller resolves hostility and a landed hit before applying.
struct CombatControl {
    bool player=false;
    std::optional<ControlEffect> active;
    ControlSource source;
    double remaining=0,immunity=0;
    bool waking=false;
    enum class Result { Applied, Rejected, CancelledBySource };
    void end(){active.reset();remaining=0;waking=false;if(player)immunity=5;}
    Result apply(ControlEffect effect,ControlSource from,bool hostile,bool self=false,double duration=-1){
        if(!hostile || self || immunity>0)return Result::Rejected;
        if(active){
            if(player && from.player && source.player && source.id==from.id){end();return Result::CancelledBySource;}
            return Result::Rejected;
        }
        const double seconds=duration<0?control_duration(effect):duration;
        if(!std::isfinite(seconds) || seconds<=0)return Result::Rejected;
        active=effect;source=from;remaining=seconds;waking=false;return Result::Applied;
    }
    void cleanse(){if(active)end();}
    void hit(double damage){
        if(damage<=0 || !std::isfinite(damage) || !active)return;
        if(*active==ControlEffect::Fear)end();
        else if(*active==ControlEffect::Sleep && !waking){waking=true;remaining=1.5;}
    }
    void advance(double dt){
        if(!std::isfinite(dt) || dt<=0)return;
        if(!active){immunity=std::max(0.,immunity-dt);return;}
        while(active && dt>=remaining){
            dt-=remaining;
            if(*active==ControlEffect::Sleep && !waking){waking=true;remaining=1.5;}
            else end();
        }
        if(active)remaining-=dt;else immunity=std::max(0.,immunity-dt);
    }
    bool blocks_basic()const{
        if(!active)return false;
        switch(*active){
        case ControlEffect::Stun:case ControlEffect::Knockback:case ControlEffect::Pull:case ControlEffect::Launch:
        case ControlEffect::Knockdown:case ControlEffect::Fear:case ControlEffect::Sleep:case ControlEffect::Disorient:return true;
        default:return false;
        }
    }
    bool blocks_skills()const{return blocks_basic() || active==ControlEffect::Silence;}
    bool blocks_mobility()const{return blocks_basic() && active!=ControlEffect::Disorient || active==ControlEffect::Root;}
    double movement_factor()const{return active==ControlEffect::Slow?.67:active==ControlEffect::Disorient?.75:1.;}
    double firing_factor()const{return active==ControlEffect::Slow?.67:1.;}
    double hit_factor()const{return active==ControlEffect::Blind?.5:1.;}
};
struct BossBreakBar {
    enum class Phase { Ready, Staggered, Recovering };
    Phase phase=Phase::Ready;
    double capacity=1000,strength=1000,remaining=0;
    std::unordered_set<uint64_t> resolvedEvents; // events that already counted in this Ready period (a repeat delivery is refused); a later phase refuses everything anyway, so the set is emptied when the bar is Ready again and holds at most one id per effect that wore the bar down
    bool apply(ControlEffect effect,uint64_t event=0){
        if(phase!=Phase::Ready)return false;
        if(event && !resolvedEvents.insert(event).second)return false;
        strength=std::max(0.,strength-control_break_strength(effect));
        if(strength==0){phase=Phase::Staggered;remaining=8;}
        return true;
    }
    bool apply_composite(const std::vector<ControlEffect>& effects,uint64_t event){
        if(effects.empty())return false;
        auto strongest=*std::max_element(effects.begin(),effects.end(),[](auto a,auto b){return control_break_strength(a)<control_break_strength(b);});
        return apply(strongest,event);
    }
    double damage_factor()const{return phase==Phase::Staggered?1.25:1.;}
    void advance(double dt){
        if(!std::isfinite(dt) || dt<=0)return;
        if(phase==Phase::Staggered){
            auto used=std::min(dt,remaining);remaining-=used;dt-=used;
            if(remaining<=0){phase=Phase::Recovering;remaining=90;}
        }
        if(phase==Phase::Recovering){remaining=std::max(0.,remaining-dt);strength=capacity*(1-remaining/90);if(remaining==0){phase=Phase::Ready;strength=capacity;resolvedEvents.clear();}}
    }
};
inline uint64_t combat_identity(const std::string& id){
    uint64_t value=1469598103934665603ull;for(unsigned char c:id){value^=c;value*=1099511628211ull;}return value<2?value+2:value;
}
struct HitChance {
    uint32_t state=0x12345678;
    bool roll(double probability){
        if(probability>=1)return true;if(probability<=0)return false;
        state^=state<<13;state^=state>>17;state^=state<<5;
        return double(state)/4294967296.0<probability;
    }
};
struct CombatActor {
    double health=100,maximum=100;
    int team=2;
    bool boss=false;
    BasicAttack attack;
    GroundSteering steering;
    std::vector<Vec3> navigationPath;
    Vec3 navigationGoal{};double navigationRetry=0,navigationBackoff=.5;
    Vec3 attackOrigin{},attackAim{},launchVelocity{};
    bool launchPending=false,launchAirborne=false;
    double tetherYank=0,tetherGuard=0;
    CombatControl control;
    BossBreakBar bar;
    bool hostile_to(int other)const{return team!=0 && other!=0 && team!=other;}
    double damage(double amount,int sourceTeam){
        if(health<=0 || !hostile_to(sourceTeam) || !std::isfinite(amount) || amount<=0)return 0;
        double taken=std::min(health,amount*(boss?bar.damage_factor():1));health-=taken;control.hit(taken);return taken;
    }
    double heal(double amount,int sourceTeam){
        if(health<=0 || sourceTeam!=team || !std::isfinite(amount) || amount<=0)return 0;
        double restored=std::min(maximum-health,amount);health+=restored;return restored;
    }
    bool apply(ControlEffect effect,ControlSource source,int sourceTeam,uint64_t event=0){
        if(health<=0 || !hostile_to(sourceTeam))return false;
        if(boss)return bar.apply(effect,event);
        auto result=control.apply(effect,source,true);
        if(result==CombatControl::Result::Applied){launchPending=effect==ControlEffect::Launch;launchAirborne=launchPending;tetherYank=tetherGuard=0;}
        return result==CombatControl::Result::Applied;
    }
    void advance(double dt){if(!launchAirborne)control.advance(dt);if(control.active!=ControlEffect::Launch)launchAirborne=launchPending=false;if(boss)bar.advance(dt);}
};
}
