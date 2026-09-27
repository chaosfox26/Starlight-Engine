#pragma once
#include "runtime_timing.hpp"
#include "interactions.hpp"
#include "gameplay_rules.hpp"
#include "movement_resources.hpp"
#include "grapple_motion.hpp"
#include "basic_attack.hpp"
#include "ability_slots.hpp"
#include "ground_navigation.hpp"
#include "combat_control.hpp"
#ifdef STARLIGHT_CHARACTER_CONTROLLER
#include "adventure_controller.hpp"
#include <memory>
#endif
#include <unordered_set>
namespace starlight::editor {
// Play state is separate from the authored scene. Collision bounds are cached
// at session start; static geometry uses conservative world-space boxes.
class AdventurePlayer {
    GameSettings settings_;
    std::vector<std::pair<std::string,Bounds>> solids_;
    std::unordered_map<std::string,Bounds> interactionBounds_;
    SceneData playScene_;
    GameplayRulesRuntime rules_;
    bool rulesEnabled_=true;
    bool jumpRequested_=false;
    bool sprintHeld_=false;
    MovementResources movement_;
    GrappleMotion grapple_;
    std::string grappleTarget_;
    Vec3 grappleOffset_{},grappleLocalNormal_{};
    Mesh grappleTerrain_;
    double mantleRemaining_=0;
    bool grappleFalling_=false;
    Vec3 presentationVelocity_{};
    BasicAttack attack_;
    uint64_t presentationEpoch_=0,shotSerial_=0,shatterSerial_=0,impactSerial_=0;
    double lastShotTime_=-1;
    AbilitySlots abilitySlots_;
    struct HealingCast {std::string caster;BasicAttack cycle;Vec3 origin{},aim{};int team=1;};
    std::vector<HealingCast> healingCasts_;
    std::unordered_map<std::string,Character> actorRoutes_;
    std::unordered_map<std::string,double> actorRouteTimes_;
    CombatControl control_;
    HitChance hitChance_;
    std::optional<ControlEffect> lastDisplayControl_;
    std::unordered_set<std::string> lastVisibleIds_;
    double health_=100;
    Vec3 controlOrigin_{},controlVelocity_{};
    bool launchAirborne_=false;
    double tetherPull_=0,tetherCooldown_=0;
    std::string selectedTarget_;
    struct Fragment {Vec3 position,velocity;double age=0;};
    std::vector<Fragment> fragments_;
    std::unordered_map<std::string,CombatActor> combatActors_;
    double dodgeRemaining_=0;
    double dodgeRollSign_=1;
    Vec3 dodgeVelocity_{};
    bool vHeld_=false,vConsumed_=false,lunging_=false;
    double vTime_=0,vRight_=0,vForward_=1;
    int posture_=0;
    bool controlHeld_=false,controlConsumed_=false;
    double controlTime_=0,slideRemaining_=0;
    Vec3 slideVelocity_{};
    std::array<double,4> lastMovementPress_{{-1,-1,-1,-1}};
    double seconds_=0;
#ifdef STARLIGHT_CHARACTER_CONTROLLER
    std::unique_ptr<AdventureController> controller_;
#endif
    std::optional<TerrainData> terrain_;
    bool blocked(Vec3 p) const {
        if(!settings_.Collision)return false;
        const double feet=p.y-1.7;
        for(const auto& solid:solids_){
            if(rules_.removed.contains(solid.first))continue;const auto& b=solid.second;
            if(b.high.y<=feet+.05 || b.low.y>=p.y+.1)continue;
            double x=std::clamp(p.x,b.low.x,b.high.x),z=std::clamp(p.z,b.low.z,b.high.z);
            if(std::hypot(p.x-x,p.z-z)<settings_.PlayerRadius)return true;
        }
        return false;
    }
  public:
    RuntimeTiming lastTiming;
    Vec3 position{};
    bool finished=false;
    const SceneData& scene() const {return playScene_;}
    double sight_range()const{return settings_.SightRange*(control_.active==ControlEffect::Blind?.5:1.);}
    bool perceived(const Character& c)const{
        auto it=combatActors_.find(c.Id);
        if(control_.active==ControlEffect::Taunt && it!=combatActors_.end() && it->second.hostile_to(1) && combat_identity(c.Id)!=control_.source.id)return false;
        auto at=c.at(seconds_);auto delta=Vec3{at.X,c.Y,at.Z}-position;
        return delta.length()<=sight_range();
    }
    SceneData visible_scene()const{
        auto copy=playScene_;
        std::erase_if(copy.Characters,[&](const Character& c){return !perceived(c);});
        if(control_.active==ControlEffect::Blind)copy.FogDensity=std::min(.3,std::max(.018,copy.FogDensity)*2);
        return copy;
    }
    const BasicAttack& basic_attack()const{return attack_;}
    const CombatControl& crowd_control()const{return control_;}
    double health()const{return health_;}
    struct CombatDisplay {
        double health=100,targetHealth=0,targetMaximum=0,breakStrength=0,breakMaximum=1000,effectSeconds=0,immunitySeconds=0,phaseSeconds=0;
        bool hasTarget=false,boss=false,waking=false;
        std::optional<ControlEffect> effect;
        BossBreakBar::Phase phase=BossBreakBar::Phase::Ready;
    };
    CombatDisplay combat_display()const{
        CombatDisplay d;d.health=health_;d.effect=control_.active;d.effectSeconds=control_.remaining;d.immunitySeconds=control_.immunity;d.waking=control_.waking;
        if(auto it=combatActors_.find(selectedTarget_);it!=combatActors_.end() && it->second.health>0){
            const auto& a=it->second;d.hasTarget=true;d.targetHealth=a.health;d.targetMaximum=a.maximum;d.boss=a.boss;
            d.breakStrength=a.bar.strength;d.breakMaximum=a.bar.capacity;d.phase=a.bar.phase;d.phaseSeconds=a.bar.remaining;
        }
        return d;
    }

    const std::unordered_map<std::string,CombatActor>& combat_actors()const{return combatActors_;}
    bool control_actor(const std::string& id,ControlEffect effect,ControlSource source,int sourceTeam=1,uint64_t event=0){
        auto it=combatActors_.find(id);return it!=combatActors_.end() && it->second.apply(effect,source,sourceTeam,event);
    }
    double heal_actor(const std::string& id,double amount,int sourceTeam=1){auto it=combatActors_.find(id);return it==combatActors_.end()?0:it->second.heal(amount,sourceTeam);}
    double restore_health(double amount){
        if(health_<=0 || !std::isfinite(amount) || amount<=0)return 0;
        double restored=std::min(100-health_,amount);health_+=restored;return restored;
    }
    void healing_cast(const std::string& caster,bool held){
        if(!rulesEnabled_)return;
        for(auto& h:healingCasts_)if(h.caster==caster){h.cycle.press(held);return;}
        if(!held || (!caster.empty() && !combatActors_.contains(caster)))return;
        HealingCast h;h.caster=caster;h.cycle.cycleSeconds=1.5;h.cycle.press(true);healingCasts_.push_back(h);
    }
    bool unobstructed(Vec3 origin,Vec3 point,const std::string& caster,const std::string& target)const{
        auto delta=point-origin;double distance=delta.length();if(distance<.01)return true;
        for(const auto& c:playScene_.Characters)if(c.Id!=caster && c.Id!=target && !rules_.removed.contains(c.Id)){
            auto moved=c;if(!c.Route.empty()){auto at=c.at(seconds_);moved.X=at.X;moved.Z=at.Z;}
            auto hit=mesh_hit(object_mesh(moved),moved,origin,delta.unit());if(hit && hit->distance<distance-.01)return false;
        }
        if(terrain_)for(double d=.1;d<distance;d+=.1){auto at=origin+delta.unit()*d;if(at.y<terrain_->height_at(at.x,at.z))return false;}
        return true;
    }
    void advance_healing(double dt){
        for(auto& h:healingCasts_){
            bool blocked=false;double rate=1;
            if(h.caster.empty()){h.origin=position-Vec3{0,1.7,0};h.aim=attack_direction();h.team=1;blocked=health_<=0 || dodging() || control_.blocks_skills();rate=control_.firing_factor();}
            else {
                auto it=combatActors_.find(h.caster);if(it==combatActors_.end()){h.cycle.clear();continue;}
                auto& actor=it->second;h.team=actor.team;blocked=actor.health<=0 || rules_.removed.contains(h.caster) || actor.control.blocks_skills() || (actor.boss && actor.bar.phase==BossBreakBar::Phase::Staggered);rate=actor.control.firing_factor();
                for(const auto& c:playScene_.Characters)if(c.Id==h.caster){auto moved=c;if(!c.Route.empty()){auto at=c.at(seconds_);moved.X=at.X;moved.Z=at.Z;}auto b=object_bounds(moved);h.origin={(b.low.x+b.high.x)*.5,b.low.y,(b.low.z+b.high.z)*.5};}
                h.aim=actor.attackAim.length()>.1?actor.attackAim:Vec3{0,0,-1};
            }
            if(!h.cycle.advance(dt,blocked,rate))continue;
            auto feet=position-Vec3{0,1.7,0},playerContact=feet+Vec3{0,.85,0};
            if(h.team==1 && surface_telegraph_contact(terrain_?&*terrain_:nullptr,h.origin,h.aim,feet-Vec3{.3,0,.3},feet+Vec3{.3,posture_==0?1.8:posture_==2?.5:1.,.3},playerContact,3,.7853981633974483) && unobstructed(h.origin+Vec3{0,.85,0},playerContact,h.caster,""))restore_health(20);
            for(const auto& c:playScene_.Characters)if(auto it=combatActors_.find(c.Id);it!=combatActors_.end() && it->second.team==h.team){
                auto moved=c;if(!c.Route.empty()){auto at=c.at(seconds_);moved.X=at.X;moved.Z=at.Z;}auto b=object_bounds(moved);auto point=(b.low+b.high)*.5;if(surface_telegraph_contact(terrain_?&*terrain_:nullptr,h.origin,h.aim,b.low,b.high,point,3,.7853981633974483) && unobstructed(h.origin+Vec3{0,.85,0},point,h.caster,c.Id))it->second.heal(20,h.team);
            }
        }
    }
    void defeat(){
        health_=0;cancel_mobility_input();control_={};control_.player=true;launchAirborne_=false;
        dodgeRemaining_=slideRemaining_=mantleRemaining_=0;grapple_.reset();grappleFalling_=false;selectedTarget_.clear();
        for(auto& h:healingCasts_)h.cycle.clear();for(auto& [id,a]:combatActors_)a.attack.clear();
    }
    double take_damage(double amount){
        if(!std::isfinite(amount) || amount<=0 || health_<=0 || dodging() || lunging())return 0;
        double applied=std::min(health_,amount);health_-=applied;control_.hit(applied);
        if(health_==0)defeat();
        return applied;
    }
    CombatControl::Result apply_control(ControlEffect effect,ControlSource source,bool hostile,bool self=false,Vec3 sourcePosition={}){
        if(health_<=0)return CombatControl::Result::Rejected;
        const auto result=control_.apply(effect,source,hostile,self);
        if(result==CombatControl::Result::Applied){
            launchAirborne_=effect==ControlEffect::Launch;tetherPull_=tetherCooldown_=0;
            controlOrigin_=sourcePosition;auto away=position-sourcePosition;away.y=0;
            away=away.length()>.01?away.unit():Vec3{0,0,1};
            controlVelocity_=effect==ControlEffect::Knockback?away*6:effect==ControlEffect::Launch?away*6+Vec3{0,6,0}:Vec3{};
            if(control_.blocks_basic())attack_.cancel();
            if(control_.blocks_mobility()){dodgeRemaining_=0;slideRemaining_=0;mantleRemaining_=0;jumpRequested_=false;vHeld_=false;vConsumed_=false;controlHeld_=false;controlConsumed_=true;}
            if(effect==ControlEffect::Root && grapple_.state==GrappleMotion::State::Pulling){
                grapple_.detach();grapple_.held=false;grapple_.velocity={0,grapple_.velocity.y,0};grappleFalling_=true;
            }
            if(effect==ControlEffect::Stun || effect==ControlEffect::Fear || effect==ControlEffect::Sleep || effect==ControlEffect::Knockback || effect==ControlEffect::Pull || effect==ControlEffect::Launch || effect==ControlEffect::Knockdown){
                if(grapple_.state!=GrappleMotion::State::Free){grapple_.detach();grapple_.held=false;grappleFalling_=true;}
            }
        }
        if(result==CombatControl::Result::CancelledBySource)launchAirborne_=false;
        return result;
    }
    void cleanse_control(){control_.cleanse();launchAirborne_=false;tetherPull_=0;}
    const std::string& selected_target()const{return selectedTarget_;}
    const AbilitySlots& ability_slots()const{return abilitySlots_;}
    void ability_button(unsigned key,bool down){if(rulesEnabled_ && abilitySlots_.action(key)==AbilityAction::BasicAttack)attack_.press(down);}
    void fire_button(bool down){ability_button('1',down);}
    void select_target(){
        std::vector<std::pair<double,std::string>> targets;
        for(const auto& c:playScene_.Characters)if((c.GameplayRole=="breakable" || (combatActors_.contains(c.Id) && combatActors_.at(c.Id).hostile_to(1))) && !rules_.removed.contains(c.Id) && perceived(c)){
            const auto b=object_bounds(c);const auto delta=(b.low+b.high)*.5-view_position();
            if(delta.length()<=25 && delta.unit().dot(camera_direction())>0)targets.push_back({1-delta.unit().dot(camera_direction())+delta.length()*.0001,c.Id});
        }
        std::sort(targets.begin(),targets.end());
        if(targets.empty()){selectedTarget_.clear();return;}
        auto it=std::find_if(targets.begin(),targets.end(),[&](const auto& t){return t.second==selectedTarget_;});
        selectedTarget_=it==targets.end() || ++it==targets.end()?targets.front().second:it->second;
    }
    void turn_actor(Character& c,Vec3 direction,double dt){
        if(c.PhysicsRotation || std::hypot(direction.x,direction.z)<1e-8)return;
        double wanted=std::atan2(-direction.x,-direction.z)*57.29577951308232;
        double difference=std::remainder(wanted-c.RotationY,360.);double change=std::clamp(difference,-360*dt,360*dt);
        if(std::abs(change)<1e-8)return;
        auto candidate=c;candidate.RotationY=std::remainder(c.RotationY+change,360.);auto proposed=object_bounds(candidate);
        for(const auto& other:playScene_.Characters)if(other.Id!=c.Id && !rules_.removed.contains(other.Id)){
            auto moved=other;if(!other.Route.empty()){auto at=other.at(seconds_);moved.X=at.X;moved.Z=at.Z;}auto b=object_bounds(moved);
            if(proposed.low.x<b.high.x && proposed.high.x>b.low.x && proposed.low.z<b.high.z && proposed.high.z>b.low.z && proposed.low.y<b.high.y-1e-5 && proposed.high.y>b.low.y+1e-5)return;
        }
        c.RotationY=candidate.RotationY;interactionBounds_[c.Id]=proposed;++rules_.revision;
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        if(controller_)controller_->move_actor(c.Id,{c.X,c.Y,c.Z},dt,c.RotationY);
#endif
    }
    struct ActorTarget {std::string id;Vec3 feet;};
    std::optional<ActorTarget> actor_target(const Character& caster,const CombatActor& actor)const{
        std::optional<ActorTarget> result;double nearest=caster.CombatSightRange*(actor.control.active==ControlEffect::Blind?.5:1.);
        auto at=caster.at(seconds_);Vec3 origin{at.X,caster.Y,at.Z};
        auto consider=[&](const std::string& id,Vec3 feet,std::uint64_t identity){
            if(actor.control.active==ControlEffect::Taunt && actor.control.source.id!=identity)return;
            auto delta=feet-origin;delta.y=0;double distance=delta.length();
            if(distance<=nearest){nearest=distance;result=ActorTarget{id,feet};}
        };
        if(health_>0 && actor.hostile_to(1))consider("",position-Vec3{0,1.7,0},1);
        for(const auto& c:playScene_.Characters){
            auto it=combatActors_.find(c.Id);
            if(c.Id==caster.Id || it==combatActors_.end() || it->second.health<=0 || !actor.hostile_to(it->second.team) || rules_.removed.contains(c.Id))continue;
            auto moved=c;if(!c.Route.empty()){auto location=c.at(seconds_);moved.X=location.X;moved.Z=location.Z;}
            auto b=object_bounds(moved);consider(c.Id,{(b.low.x+b.high.x)*.5,b.low.y,(b.low.z+b.high.z)*.5},combat_identity(c.Id));
        }
        return result;
    }
    void advance_pursuit(double dt){
        if(!rulesEnabled_ || !std::isfinite(dt) || dt<=0)return;
        dt=std::min(dt,.05);
        for(auto& c:playScene_.Characters){
            auto found=combatActors_.find(c.Id);if(found==combatActors_.end())continue;
            const bool hasRoute=actorRoutes_.contains(c.Id);
            const bool fleeing=found->second.control.active==ControlEffect::Fear;
            const bool displaced=found->second.control.active==ControlEffect::Knockback || found->second.control.active==ControlEffect::Pull;
            const bool launched=found->second.launchAirborne;
            const bool tethered=found->second.control.active==ControlEffect::Tether;
            if(!c.CombatPursuit && !hasRoute && !fleeing && !displaced && !launched && !tethered)continue;
            auto& actor=found->second;auto victim=actor_target(c,actor);Vec3 origin{c.X,c.Y,c.Z};
            bool followingRoute=hasRoute && (!c.CombatPursuit || !victim);
            if(followingRoute){
                if(!actor.control.blocks_mobility() && !(actor.boss && actor.bar.phase==BossBreakBar::Phase::Staggered))actorRouteTimes_[c.Id]+=dt*actor.control.movement_factor();
                auto at=actorRoutes_.at(c.Id).at(actorRouteTimes_[c.Id]);victim=ActorTarget{"",{at.X,c.Y,at.Z}};
            }
            Vec3 effectSource=position;
            if(fleeing || displaced || launched || tethered){
                Vec3 source=position;
                for(const auto& other:playScene_.Characters)if(combat_identity(other.Id)==actor.control.source.id){auto at=other.at(seconds_);source={at.X,other.Y,at.Z};break;}
                effectSource=source;
                auto away=origin-source;away.y=0;if(away.length()<.01)away={0,0,1};
                if(!tethered)victim=ActorTarget{"",origin+away.unit()*10};
                else if(!victim)victim=ActorTarget{"",origin};
            }
            if(!victim){actor.steering.stop();continue;}auto target=victim->feet;target.y=c.Y;
            if(actor.health<=0 || (actor.control.blocks_mobility() && !fleeing && !displaced && !launched) || (actor.boss && actor.bar.phase==BossBreakBar::Phase::Staggered) || (target-origin).length()>20){actor.steering.stop();continue;}
            auto bounds=object_bounds(c);double radius=std::max(bounds.high.x-bounds.low.x,bounds.high.z-bounds.low.z)*.5;
            double spacing=std::max(2.,radius+settings_.PlayerRadius+.2);
            auto routeClear=[&](Vec3 from,Vec3 to){
                auto span=to-from;int samples=std::max(1,int(std::ceil(span.length()/.2)));
                for(int i=1;i<=samples;i++){
                    auto at=from+span*(double(i)/samples);auto offset=at-origin;
                    if(terrain_){double oldHeight=terrain_->height_at(from.x,from.z),height=terrain_->height_at(at.x,at.z);if(std::abs(height-oldHeight)>std::hypot(at.x-from.x,at.z-from.z)+.05)return false;offset.y=height-terrain_->height_at(origin.x,origin.z);}
                    auto low=bounds.low+offset,high=bounds.high+offset;
                    for(const auto& other:playScene_.Characters)if(other.Id!=c.Id && !rules_.removed.contains(other.Id)){
                        auto obstacle=other;if(!other.Route.empty()){auto at=other.at(seconds_);obstacle.X=at.X;obstacle.Z=at.Z;}auto b=object_bounds(obstacle);
                        if(low.x<b.high.x+.03 && high.x>b.low.x-.03 && low.z<b.high.z+.03 && high.z>b.low.z-.03 && low.y<b.high.y-.05 && high.y>b.low.y+.05)return false;
                    }
                }return true;
            };
            auto steeringTarget=target;double steeringSpacing=followingRoute?.001:spacing;
            actor.navigationRetry=std::max(0.,actor.navigationRetry-dt);
            if(!fleeing && !displaced && !launched && actor.tetherYank<=0){
                auto toward=target-origin;double length=std::hypot(toward.x,toward.z);auto goal=length>steeringSpacing?target-toward.unit()*steeringSpacing:origin;
                if(routeClear(origin,goal)){actor.navigationPath.clear();}
                else {
                    if((actor.navigationGoal-goal).length()>1)actor.navigationPath.clear();
                    while(!actor.navigationPath.empty() && (actor.navigationPath.front()-origin).length()<.2)actor.navigationPath.erase(actor.navigationPath.begin());
                    if(!actor.navigationPath.empty() && !routeClear(origin,actor.navigationPath.front()))actor.navigationPath.clear();
                    if(actor.navigationPath.empty() && actor.navigationRetry<=0){actor.navigationPath=ground_path(origin,goal,routeClear);actor.navigationGoal=goal;actor.navigationRetry=.5;}
                    while(!actor.navigationPath.empty() && (actor.navigationPath.front()-origin).length()<.2)actor.navigationPath.erase(actor.navigationPath.begin());
                    if(!actor.navigationPath.empty()){steeringTarget=actor.navigationPath.front();steeringSpacing=0;}
                }
            }
            auto delta=actor.steering.step(origin,steeringTarget,dt,(followingRoute?c.Speed:2.5)*actor.control.movement_factor(),steeringSpacing);
            if(displaced){
                auto direction=actor.control.active==ControlEffect::Pull?effectSource-origin:origin-effectSource;direction.y=0;
                double speed=actor.control.active==ControlEffect::Pull?std::min(16.,std::max(0.,direction.length()-1)/dt):6.;
                if(direction.length()<.01)direction={0,0,1};delta=direction.unit()*speed*dt;actor.steering.stop();
            }
            if(launched){
                if(actor.launchPending){auto away=origin-effectSource;away.y=0;if(away.length()<.01)away={0,0,1};actor.launchVelocity=away.unit()*6+Vec3{0,6,0};actor.launchPending=false;}
                actor.launchVelocity.y-=9.81*dt;delta=actor.launchVelocity*dt;actor.steering.stop();
            }
            actor.tetherGuard=std::max(0.,actor.tetherGuard-dt);
            if(tethered){auto offset=origin-effectSource;offset.y=0;
                if(offset.length()>6 && actor.tetherYank<=0 && actor.tetherGuard<=0){actor.tetherYank=.5;actor.tetherGuard=.5;}
                if(actor.tetherYank>0){actor.tetherYank=std::max(0.,actor.tetherYank-dt);delta=offset.unit()*-std::min(16.*dt,std::max(0.,offset.length()-5.5));actor.steering.stop();}
            }else actor.tetherYank=0;
            if(!launched && terrain_)delta.y=terrain_->height_at(c.X+delta.x,c.Z+delta.z)-terrain_->height_at(c.X,c.Z);
            auto obstructed=[&](Vec3 offset){
                auto low=bounds.low+offset,high=bounds.high+offset;
                for(const auto& other:playScene_.Characters)if(other.Id!=c.Id && !rules_.removed.contains(other.Id)){
                    auto obstacle=other;if(!other.Route.empty()){auto at=other.at(seconds_);obstacle.X=at.X;obstacle.Z=at.Z;}
                    auto b=object_bounds(obstacle);
                    if(low.x<b.high.x && high.x>b.low.x && low.z<b.high.z && high.z>b.low.z && low.y<b.high.y-1e-5 && high.y>b.low.y+1e-5)return true;
                }
                return false;
            };
            Vec3 allowed{};bool landed=false;
            for(int axis=0;axis<3;axis++){
                Vec3 portion=axis==0?Vec3{delta.x,0,0}:axis==1?Vec3{0,0,delta.z}:Vec3{0,delta.y,0};
                int steps=std::max(1,int(std::ceil(portion.length()/.025)));auto increment=portion*(1./steps);
                for(int i=0;i<steps;i++){
                    auto next=allowed+increment;double floor=terrain_?terrain_->height_at(c.X+next.x,c.Z+next.z):0;
                    if(launched && axis==2 && increment.y<0 && bounds.low.y+next.y<=floor){allowed.y=floor-bounds.low.y;landed=true;break;}
                    if(!launched && terrain_ && std::abs(floor-terrain_->height_at(c.X,c.Z))>.05){actor.steering.stop();break;}
                    if(obstructed(next)){actor.steering.stop();if(launched && axis==2){landed=increment.y<0;actor.launchVelocity.y=0;}break;}
                    allowed=next;
                }
            }
            delta=allowed;
            if(landed){actor.launchAirborne=false;actor.launchVelocity={};actor.control.remaining=1;}
            if(delta.length()<1e-9)continue;
            c.X+=delta.x;c.Y+=delta.y;c.Z+=delta.z;
            if(!c.CombatAutoAttack && !fleeing && !displaced && !launched)turn_actor(c,delta,dt);
            interactionBounds_[c.Id]=object_bounds(c);++rules_.revision;
#ifdef STARLIGHT_CHARACTER_CONTROLLER
            if(controller_)controller_->move_actor(c.Id,{c.X,c.Y,c.Z},dt,c.RotationY);
#endif
        }
    }
    void advance_enemy_attacks(double dt){
        if(!rulesEnabled_)return;
        std::vector<std::string> defeated;
        for(auto& c:playScene_.Characters){
            auto it=combatActors_.find(c.Id);if(it==combatActors_.end())continue;
            auto& actor=it->second;
            auto moved=c;if(!c.Route.empty()){auto at=c.at(seconds_);moved.X=at.X;moved.Z=at.Z;}
            auto bounds=object_bounds(moved);
            Vec3 origin{(bounds.low.x+bounds.high.x)*.5,bounds.low.y,(bounds.low.z+bounds.high.z)*.5};
            auto victim=actor_target(c,actor);
            auto delta=victim?victim->feet-origin:Vec3{};delta.y=0;
            bool eligible=c.CombatAutoAttack && victim.has_value() && actor.health>0 && !rules_.removed.contains(c.Id);
            bool blocked=!eligible || actor.tetherYank>0 || actor.control.blocks_basic() || (actor.boss && actor.bar.phase==BossBreakBar::Phase::Staggered);
            if(eligible && !actor.control.blocks_basic())turn_actor(c,delta,dt);
            double facing=c.RotationY*.0174532925199433;
            actor.attackOrigin=origin;actor.attackAim={-std::sin(facing),0,-std::cos(facing)}; // Cone and hit share current bounded actor facing.
            actor.attack.press(eligible && delta.length()<=3);
            if(!actor.attack.advance(dt,blocked,actor.control.firing_factor()))continue;
            auto point=victim->feet+Vec3{0,.85,0};
            Vec3 targetLow=victim->feet-Vec3{.3,0,.3},targetHigh=victim->feet+Vec3{.3,posture_==0?1.8:posture_==2?.5:1.,.3};
            if(!victim->id.empty())for(const auto& target:playScene_.Characters)if(target.Id==victim->id){
                auto moved=target;if(!target.Route.empty()){auto at=target.at(seconds_);moved.X=at.X;moved.Z=at.Z;}
                auto targetBounds=object_bounds(moved);targetLow=targetBounds.low;targetHigh=targetBounds.high;break;
            }
            if(!surface_telegraph_contact(terrain_?&*terrain_:nullptr,actor.attackOrigin,actor.attackAim,targetLow,targetHigh,point,3,.7853981633974483))continue;
            auto eye=actor.attackOrigin+Vec3{0,1,0};
            if(unobstructed(eye,point,c.Id,victim->id) && hitChance_.roll(actor.control.hit_factor())){
                if(victim->id.empty()){
                    take_damage(10);
                    if(c.CombatControlEffect>=0 && c.CombatControlEffect<14)apply_control(static_cast<ControlEffect>(c.CombatControlEffect),{combat_identity(c.Id),false},true,false,origin);
                }else if(auto target=combatActors_.find(victim->id);target!=combatActors_.end()){
                    target->second.damage(10,actor.team);
                    if(c.CombatControlEffect>=0 && c.CombatControlEffect<14)target->second.apply(static_cast<ControlEffect>(c.CombatControlEffect),{combat_identity(c.Id),false},actor.team);
                    if(target->second.health<=0){target->second.attack.clear();target->second.steering.stop();defeated.push_back(victim->id);}
                }
            }
        }
        for(const auto& id:defeated){
            rules_.remove_object(playScene_,id);rules_.emit("object-broken",id,playScene_);
            if(selectedTarget_==id)selectedTarget_.clear();
        }
    }
    void advance_combat(double dt){
        for(auto& [id,actor]:combatActors_){
            actor.advance(dt);
            if(actor.control.active==ControlEffect::Taunt){
                bool valid=actor.control.source.id==1 && health_>0 && actor.hostile_to(1);
                for(const auto& [otherId,other]:combatActors_)if(combat_identity(otherId)==actor.control.source.id && other.health>0 && actor.hostile_to(other.team) && !rules_.removed.contains(otherId))valid=true;
                if(!valid)actor.control.end();
            }
        }
        advance_pursuit(dt);
        advance_enemy_attacks(dt);
        advance_healing(dt);
        for(auto& f:fragments_){f.age+=dt;f.velocity.y-=9.81*dt;f.position=f.position+f.velocity*dt;}
        std::erase_if(fragments_,[](const auto& f){return f.age>=2;});
        if(!launchAirborne_)control_.advance(dt);
        if(control_.active==ControlEffect::Taunt){
            bool valid=false;for(const auto& [id,a]:combatActors_)if(combat_identity(id)==control_.source.id && a.health>0 && a.hostile_to(1))valid=true;
            if(!valid)control_.end();
        }
        if(!selectedTarget_.empty()){
            bool valid=false;for(const auto& c:playScene_.Characters)if(c.Id==selectedTarget_ && !rules_.removed.contains(c.Id) && perceived(c))valid=true;
            if(!valid)selectedTarget_.clear();
        }
        if(lastDisplayControl_!=control_.active){lastDisplayControl_=control_.active;++rules_.revision;}
        std::unordered_set<std::string> visibleIds;
        for(const auto& c:playScene_.Characters)if(!rules_.removed.contains(c.Id) && perceived(c))visibleIds.insert(c.Id);
        if(visibleIds!=lastVisibleIds_){lastVisibleIds_=std::move(visibleIds);++rules_.revision;}
        if(!rulesEnabled_ || !attack_.advance(dt,health_<=0 || dodging() || tetherPull_>0 || control_.blocks_basic(),control_.firing_factor()))return;
        ++shotSerial_;lastShotTime_=seconds_;
        std::vector<std::string> hit;
        for(const auto& c:playScene_.Characters)if((c.GameplayRole=="breakable" || (combatActors_.contains(c.Id) && combatActors_.at(c.Id).hostile_to(1))) && !rules_.removed.contains(c.Id) && perceived(c)){
            auto moved=c;if(!c.Route.empty()){auto at=c.at(seconds_);moved.X=at.X;moved.Z=at.Z;}
            auto b=object_bounds(moved);auto point=(b.low+b.high)*.5;
            if(!surface_telegraph_contact(terrain_?&*terrain_:nullptr,position-Vec3{0,1.7,0},attack_direction(),b.low,b.high,point))continue;
            if(unobstructed(position-Vec3{0,.7,0},point,"",c.Id) && hitChance_.roll(control_.hit_factor()))hit.push_back(c.Id);
        }
        for(const auto& id:hit){
            if(auto it=combatActors_.find(id);it!=combatActors_.end()){
                if(!it->second.hostile_to(1))continue;
                it->second.damage(10,1);if(it->second.health>0)continue;
            }
            for(const auto& c:playScene_.Characters)if(c.Id==id){auto b=object_bounds(c);auto center=(b.low+b.high)*.5;
                for(int i=0;i<16;i++){double angle=i*2.3999632297;fragments_.push_back({center,{std::cos(angle)*(1+i%3),2.+i%4,std::sin(angle)*(1+i%3)},0});}
                if(fragments_.size()>256)fragments_.erase(fragments_.begin(),fragments_.begin()+(fragments_.size()-256));break;
            }
            rules_.remove_object(playScene_,id);rules_.emit("object-broken",id,playScene_);if(selectedTarget_==id)selectedTarget_.clear();
            ++shatterSerial_;
        }
        finished=rules_.finished;
    }
    double yaw=0,pitch=0;
    int posture() const {return posture_;} // 0 standing, 1 crouched, 2 prone, 3 sliding
    // Asset-independent animation input, derived from actual controller movement.
    // Reading presentation never changes simulation or the authored scene.
    struct Presentation {
        enum class Motion { Idle, Walk, Sprint, Jump, Fall, Crouch, Crawl, Slide, Dodge, Lunge, GrapplePull, WallHold, Climb, Swing };
        Motion motion=Motion::Idle;
        Vec3 feet{},velocity{},tetherAnchor{};
        double facing=0,bodyHeight=1.8;
        bool showBody=true,showTether=false;
        bool showWeapons=false,audioActive=false;
        Vec3 eye{},viewAim{0,0,-1},viewUp{0,1,0};
        uint64_t epoch=0,shots=0,shatters=0,impacts=0;
        double shotFlash=0;
        bool showTelegraph=false;
        const TerrainData* telegraphTerrain=nullptr;
        double channelProgress=0;
        double telegraphRange=basicAttackRange,telegraphHalfAngle=basicAttackHalfAngle;
        TelegraphKind telegraphKind=TelegraphKind::Friendly;
        Vec3 attackOrigin{},attackAim{};
        std::optional<Bounds> targetBounds;
        std::vector<Vec3> fragments;
        struct ActorTelegraph {Vec3 origin,aim;double progress;TelegraphKind kind;};
        std::vector<ActorTelegraph> actorTelegraphs;
        std::optional<ControlEffect> control;
        double poseTime=0;
    };
    Presentation presentation() const {
        Presentation p;p.telegraphTerrain=terrain_?&*terrain_:nullptr;
        p.showWeapons=rulesEnabled_ && health_>0;p.audioActive=health_>0;
        p.eye=camera_eye();p.viewAim=camera_direction();p.viewUp=camera_up();
        p.epoch=presentationEpoch_;p.shots=shotSerial_;p.shatters=shatterSerial_;p.impacts=impactSerial_;
        p.shotFlash=health_>0 && lastShotTime_>=0?std::clamp(1-(seconds_-lastShotTime_)/.12,0.,1.):0;
        p.control=control_.active;p.poseTime=seconds_;
        auto casterVisible=[&](const std::string& id){if(id.empty())return true;for(const auto& c:playScene_.Characters)if(c.Id==id)return perceived(c);return false;};
        for(const auto& h:healingCasts_)if(h.cycle.winding && casterVisible(h.caster))p.actorTelegraphs.push_back({h.origin,h.aim,std::clamp(1-h.cycle.windup/h.cycle.cycleSeconds,0.,1.),telegraph_kind(h.team==1,true)});
        for(const auto& [id,actor]:combatActors_)if(casterVisible(id) && actor.attack.winding && actor.health>0 && !rules_.removed.contains(id))
            p.actorTelegraphs.push_back({actor.attackOrigin,actor.attackAim,std::clamp(1-actor.attack.windup/actor.attack.cycleSeconds,0.,1.),telegraph_kind(actor.team==1,false)});
        p.feet=position-Vec3{0,1.7,0};p.velocity=presentationVelocity_;p.facing=yaw;
        p.bodyHeight=posture_==0?1.8:posture_==2?.5:1.;p.showBody=cameraDistance>.001;
        p.showTether=grapple_.state!=GrappleMotion::State::Free;p.tetherAnchor=grapple_.hook_position();
        p.showTelegraph=rulesEnabled_ && attack_.winding;p.attackOrigin=p.feet;p.attackAim=attack_direction();
        p.channelProgress=attack_.winding?std::clamp(1-attack_.windup/attack_.cycleSeconds,0.,1.):0;
        for(const auto& f:fragments_)p.fragments.push_back(f.position);
        for(const auto& c:playScene_.Characters)if(c.Id==selectedTarget_ && perceived(c)){auto moved=c;if(!c.Route.empty()){auto at=c.at(seconds_);moved.X=at.X;moved.Z=at.Z;}p.targetBounds=object_bounds(moved);break;}
        using M=Presentation::Motion;
        const double horizontal=std::hypot(p.velocity.x,p.velocity.z);
        if(dodging())p.motion=M::Dodge;
        else if(lunging())p.motion=M::Lunge;
        else if(grapple_.state==GrappleMotion::State::Pulling)p.motion=M::GrapplePull;
        else if(grapple_.state==GrappleMotion::State::Swing)p.motion=M::Swing;
        else if(grapple_.state==GrappleMotion::State::Wall)p.motion=std::abs(p.velocity.y)>.1?M::Climb:M::WallHold;
        else if(!grounded())p.motion=p.velocity.y>.1?M::Jump:M::Fall;
        else if(posture_==3)p.motion=M::Slide;
        else if(posture_==2)p.motion=M::Crawl;
        else if(posture_==1)p.motion=M::Crouch;
        else if(horizontal>.1)p.motion=horizontal>settings_.MoveSpeed*1.1?M::Sprint:M::Walk;
        return p;
    }
    bool set_posture(int value){
        if(value<0 || value>3)return false;
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        if(settings_.Collision){
            if(!controller_)controller_=std::make_unique<AdventureController>(playScene_,position);
            if(!controller_->height(value==0?1.8:value==2?.5:1.))return false;
        }
#endif
        posture_=value;return true;
    }
    void posture_button(bool down,double right,double forward,double elapsed=0){
        if(health_<=0 || control_.blocks_mobility())return;
        if(down){
            if(!controlHeld_){
                controlHeld_=true;controlConsumed_=false;controlTime_=0;
                double length=std::hypot(right,forward);
                if(length>.1 && posture_==0 && grounded() && set_posture(3)){
                    slideVelocity_={(right*std::cos(yaw)+forward*std::sin(yaw))*8/length,0,(right*std::sin(yaw)-forward*std::cos(yaw))*8/length};
                    slideRemaining_=.6;controlConsumed_=true;
                }
            }
            if(std::isfinite(elapsed) && elapsed>0)controlTime_+=elapsed;
            if(!controlConsumed_ && controlTime_>=.35 && std::hypot(right,forward)<.1){set_posture(2);controlConsumed_=true;}
        }else if(controlHeld_){
            if(!controlConsumed_)set_posture(posture_==0?1:0);
            controlHeld_=false;controlConsumed_=false;controlTime_=0;
        }
    }
    Vec3 view_position() const {return position+Vec3{0,posture_==0?0:posture_==2?-1.3:-.8,0};}
    double cameraYaw=0,cameraPitch=0,cameraDistance=5;
    void zoom(double steps){if(std::isfinite(steps)){cameraDistance=std::clamp(cameraDistance-steps*.5,0.,12.);if(cameraDistance<=.15)cameraDistance=0;}}
    void orbit_camera(double horizontal,double vertical){
        if(!std::isfinite(horizontal)||!std::isfinite(vertical))return;
        const double factor=settings_.LookSensitivity*.0174532925199433;
        cameraYaw=std::remainder(cameraYaw+horizontal*factor,6.283185307179586);
        cameraPitch=std::clamp(cameraPitch-vertical*factor,-1.3962634015954636,1.3962634015954636);
    }
    Vec3 camera_direction() const {
        double reaction=0;
        if(cameraDistance<=.001 && !settings_.ReducedCameraMotion){
            if(control_.active==ControlEffect::Launch && !launchAirborne_)reaction=1.2*std::clamp(control_.remaining,0.,1.);
            else if(control_.active==ControlEffect::Knockback || control_.active==ControlEffect::Pull)reaction=.15*std::sin(control_.remaining*35)*std::clamp(control_.remaining*2,0.,1.);
            else if(control_.active==ControlEffect::Knockdown)reaction=-.35*std::sin(std::clamp(control_.remaining,0.,1.)*3.141592653589793);
        }
        const double angle=std::clamp(cameraPitch+reaction,-1.55,1.55);
        return {std::sin(cameraYaw)*std::cos(angle),std::sin(angle),-std::cos(cameraYaw)*std::cos(angle)};
    }
    Vec3 attack_direction()const{return grounded()?Vec3{std::sin(yaw),0,-std::cos(yaw)}:Vec3{std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)};}
    Vec3 camera_up() const {
        auto forward=camera_direction();auto right=forward.cross({0,1,0}).unit();auto up=right.cross(forward);
        if(cameraDistance>.001 || !dodging() || settings_.ReducedCameraMotion)return up;
        const double phase=std::clamp(1.-dodgeRemaining_/.25,0.,1.);
        const double angle=dodgeRollSign_*6.283185307179586*(phase*phase*(3-2*phase));
        return up*std::cos(angle)+right*std::sin(angle);
    }
    Vec3 camera_eye() const {
        if(cameraDistance<=.001)return view_position();
        auto backward=camera_direction()*-1.;double distance=cameraDistance;
        for(const auto& object:playScene_.Characters){
            if(object.GameplayRole=="key" || object.GameplayRole=="exit")continue;
            auto moved=object;if(!object.Route.empty()){auto point=object.at(seconds_);moved.X=point.X;moved.Z=point.Z;}
            auto hit=mesh_hit(object_mesh(moved),moved,view_position(),backward);
            if(hit)distance=std::min(distance,std::max(0.,hit->distance-.2));
        }
        // Sample the short camera boom against terrain to avoid looking through hills.
        for(double d=.1;d<=distance;d+=.1){auto p=view_position()+backward*d;
            if(terrain_ && p.y<terrain_->height_at(p.x,p.z)+.15){distance=std::max(0.,d-.1);break;}}
        return view_position()+backward*distance;
    }
    bool standing_inside_solid() const {return blocked(position);}
    void rebuild_for_authoring(const SceneData& scene){
        auto savedPosition=position;double savedYaw=yaw,savedPitch=pitch,savedCameraYaw=cameraYaw,savedCameraPitch=cameraPitch,savedDistance=cameraDistance;
        start(scene,false);position=savedPosition;yaw=savedYaw;pitch=savedPitch;cameraYaw=savedCameraYaw;cameraPitch=savedCameraPitch;cameraDistance=savedDistance;
    }
    const GameplayRulesRuntime& rules() const {return rules_;}
    double seconds() const {return seconds_;}
    void jump(){
        if(health_<=0 || control_.blocks_mobility())return;
        if(grapple_.state==GrappleMotion::State::Wall){grapple_.wall_jump();grappleFalling_=true;return;}
        if(posture_!=0 && !set_posture(0))return;
        jumpRequested_=true;
    }
    const GrappleMotion& grapple_motion() const {return grapple_;}
    void grapple_button(bool down){
        if(!down){auto before=grapple_.state;if(grapple_.flying())grapple_.velocity=presentationVelocity_;grapple_.release();if(before!=GrappleMotion::State::Free && grapple_.state==GrappleMotion::State::Free)grappleFalling_=true;return;}
        if(health_<=0 || control_.blocks_mobility())return;
        if(grapple_.held)return;
        std::optional<MeshHit> nearest;std::string id;Vec3 offset{},localNormal{};int grappleMode=0;
        for(const auto& object:playScene_.Characters){
            if(rules_.removed.contains(object.Id) || (object.Shape && (*object.Shape=="light" || *object.Shape=="fire" || *object.Shape=="shadowenemy-smoke")))continue;
            auto moved=object;if(!object.Route.empty()){auto at=object.at(seconds_);moved.X=at.X;moved.Z=at.Z;}
            auto hit=mesh_hit(object_mesh(moved),moved,view_position(),camera_direction());
            if(hit && hit->distance<=25 && (!nearest || hit->distance<nearest->distance)){nearest=hit;id=object.Id;grappleMode=object.GrappleMode;offset=local_point(moved,hit->point);localNormal=local_point(moved,hit->point+hit->normal)-offset;}
        }
        if(!grappleTerrain_.indices.empty()){
            Character ground;auto hit=mesh_hit(grappleTerrain_,ground,view_position(),camera_direction());
            if(hit && hit->distance<=25 && (!nearest || hit->distance<nearest->distance)){nearest=hit;id="@terrain";grappleMode=0;offset=hit->point;localNormal=hit->normal;}
        }
        if(grapple_.press(movement_,nearest.has_value(),position,nearest?nearest->point:Vec3{},nearest?nearest->normal:Vec3{0,1,0},nearest && (grappleMode==2 || (grappleMode==0 && nearest->normal.y<-.5)))){
            grapple_.velocity=presentationVelocity_;grappleTarget_=id;grappleOffset_=offset;grappleLocalNormal_=localNormal;grappleFalling_=false;mantleRemaining_=0;
        }
    }
    void sprint(bool held){sprintHeld_=held;}
    const MovementResources& movement_resources() const {return movement_;}
    void mobility_button(bool down,double right,double forward,double elapsed=0){
        if(down){
            if(!vHeld_){vHeld_=true;vConsumed_=false;vTime_=0;vRight_=right;vForward_=forward;}
            if(std::isfinite(elapsed) && elapsed>0)vTime_+=elapsed;
            if(!vConsumed_ && vTime_>=.22){
                vConsumed_=true;
                if(std::hypot(right,forward)>.01)lunge();
            }
        }else if(vHeld_){
            if(!vConsumed_)dodge(vRight_,vForward_);
            vHeld_=false;vConsumed_=false;vTime_=0;
        }
    }
    void cancel_mobility_input(){for(auto& h:healingCasts_)if(h.caster.empty())h.cycle.clear();attack_.clear();grapple_.detach();grapple_.held=false;grappleFalling_=false;vHeld_=false;vConsumed_=false;vTime_=0;controlHeld_=false;controlConsumed_=false;controlTime_=0;sprintHeld_=false;jumpRequested_=false;lastMovementPress_.fill(-1);}
    bool lunge(){
        if(health_<=0 || !grounded() || control_.blocks_mobility())return false;
        if(dodgeRemaining_>0 || !movement_.spend_dodge(2))return false;
        dodgeVelocity_={std::sin(yaw)*12,0,-std::cos(yaw)*12};
        dodgeRemaining_=.5;lunging_=true;jumpRequested_=true;return true;
    }
    bool dodging() const {return dodgeRemaining_>0 && !lunging_;}
    bool lunging() const {return dodgeRemaining_>0 && lunging_;}
    bool dodge(double right,double forward){
        if(health_<=0 || control_.blocks_mobility())return false;
        if(dodgeRemaining_>0 || !std::isfinite(right) || !std::isfinite(forward))return false;
        double length=std::hypot(right,forward);
        if(length<.001){right=0;forward=1;length=1;}
        if(!movement_.spend_dodge())return false;
        grapple_.detach();grappleFalling_=false;
        // Initial tuning: three metres over 0.25 seconds, collision-resolved.
        dodgeVelocity_={(right*std::cos(yaw)+forward*std::sin(yaw))*12/length,0,
                        (right*std::sin(yaw)-forward*std::cos(yaw))*12/length};
        dodgeRemaining_=.25;dodgeRollSign_=right<0?-1:1;lunging_=false;return true;
    }
    void movement_press(int key,double now){
        int i=key=='W'?0:key=='A'?1:key=='S'?2:key=='D'?3:-1;
        if(i<0 || !std::isfinite(now))return;
        const double elapsed=now-lastMovementPress_[i];
        if(lastMovementPress_[i]>=0 && elapsed>=0 && elapsed<=.25){
            lastMovementPress_[i]=-1;
            dodge(i==1?-1:i==3?1:0,i==0?1:i==2?-1:0);
        }else lastMovementPress_[i]=now;
    }
    bool grounded() const {
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        return controller_ && controller_->grounded;
#else
        return false;
#endif
    }
    void update_rules(){if(rulesEnabled_){
        rules_.areas(position,playScene_);
        if(grounded()){
            std::vector<std::string> supports;
            for(const auto& object:playScene_.Characters){const auto& b=interactionBounds_.at(object.Id);
                if(position.x>=b.low.x && position.x<=b.high.x && position.z>=b.low.z && position.z<=b.high.z && std::abs(position.y-1.7-b.high.y)<.15)supports.push_back(object.Id);}
            for(const auto& id:supports)rules_.emit("stand-on-object",id,playScene_);
        }
        finished=rules_.finished;
    }}
    void start(const SceneData& scene,bool gameplayRules=true){
        if(!scene.Game)throw std::runtime_error("Choose an adventure preset first");
        ++presentationEpoch_;shotSerial_=shatterSerial_=impactSerial_=0;lastShotTime_=-1;
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        controller_.reset();
#endif
        grapple_.reset();grappleTarget_.clear();grappleFalling_=false;
        grappleTerrain_={};mantleRemaining_=0;
        if(scene.Terrain)for(const auto& part:terrain_geometry(*scene.Terrain)){int offset=int(grappleTerrain_.vertices.size());grappleTerrain_.vertices.insert(grappleTerrain_.vertices.end(),part.mesh.vertices.begin(),part.mesh.vertices.end());for(int index:part.mesh.indices)grappleTerrain_.indices.push_back(index+offset);}
        seconds_=0;jumpRequested_=false;sprintHeld_=false;movement_={};
        dodgeRemaining_=0;dodgeVelocity_={};presentationVelocity_={};lastMovementPress_.fill(-1);
        cancel_mobility_input();attack_={};hitChance_={};lastDisplayControl_.reset();lastVisibleIds_.clear();control_={};control_.player=true;health_=100;controlOrigin_=controlVelocity_={};launchAirborne_=false;tetherPull_=tetherCooldown_=0;selectedTarget_.clear();fragments_.clear();combatActors_.clear();healingCasts_.clear();lunging_=false;
        for(const auto& c:scene.Characters)if(c.CombatHealth>0){CombatActor actor;actor.health=actor.maximum=c.CombatHealth;actor.team=c.CombatTeam;actor.boss=c.CombatBoss;actor.attack.cycleSeconds=1.5;combatActors_.emplace(c.Id,actor);}
        posture_=0;controlHeld_=controlConsumed_=false;controlTime_=slideRemaining_=0;slideVelocity_={};
        scene.validate();playScene_=scene;actorRoutes_.clear();actorRouteTimes_.clear();
        if(gameplayRules)for(auto& c:playScene_.Characters)if(c.CombatHealth>0 && !c.Route.empty()){actorRoutes_.emplace(c.Id,c);actorRouteTimes_.emplace(c.Id,0);c.Route.clear();}
        rules_.reset(scene.Rules);rulesEnabled_=gameplayRules;finished=false;settings_=*scene.Game;terrain_=scene.Terrain;solids_.clear();interactionBounds_.clear();
        for(const auto& object:scene.Characters){
            auto bounds=object_bounds(object);
            interactionBounds_.emplace(object.Id,bounds);
            if(object.Shape && (*object.Shape=="light" || *object.Shape=="fire" || *object.Shape=="shadowenemy-smoke"))continue;
            if(object.GameplayRole!="key" && object.GameplayRole!="exit")solids_.push_back({object.Id,bounds});
        }
        position={settings_.SpawnX,settings_.SpawnY,settings_.SpawnZ};yaw=pitch=0;
        cameraYaw=cameraPitch=0;cameraDistance=5;
    }
    void look(double horizontal,double vertical){
        if(!std::isfinite(horizontal)||!std::isfinite(vertical))return;
        orbit_camera(horizontal,vertical);yaw=cameraYaw;pitch=cameraPitch;
    }
    void move(double right,double forward,double seconds){
        lastTiming={};lastTiming.inputSampleTime=timing_seconds();ScopedTiming simulationTimer{lastTiming.simulationMs};
        if(!std::isfinite(right)||!std::isfinite(forward)||!std::isfinite(seconds)||seconds<=0)return;
        right=std::clamp(right,-1.,1.);forward=std::clamp(forward,-1.,1.);
        double length=std::max(1.,std::hypot(right,forward));
        const double dt=std::min(seconds,.05);
        if(position.y<settings_.SpawnY-100 && health_>0)defeat();
        {ScopedTiming timer{lastTiming.combatAiMs};advance_combat(dt);}
        if(health_<=0 || control_.blocks_mobility()){right=forward=0;jumpRequested_=false;}
        const double sprintFraction=movement_.advance(dt,sprintHeld_,right!=0 || forward!=0);
        const double moveSpeed=settings_.MoveSpeed*(1.+.5*sprintFraction)*(posture_==2?.25:posture_==1?.5:1.)*control_.movement_factor();
        const double dodgeFraction=std::min(dt,dodgeRemaining_)/dt;
        dodgeRemaining_=std::max(0.,dodgeRemaining_-dt);if(dodgeRemaining_<1e-9)dodgeRemaining_=0;
        Vec3 velocity{(right*std::cos(yaw)+forward*std::sin(yaw))*moveSpeed/length,0,
                      (right*std::sin(yaw)-forward*std::cos(yaw))*moveSpeed/length};
        velocity=velocity*(1-dodgeFraction)+dodgeVelocity_*(dodgeFraction*control_.movement_factor());
        if(slideRemaining_>0){velocity=slideVelocity_*((.25+.75*slideRemaining_/.6)*control_.movement_factor());slideRemaining_=std::max(0.,slideRemaining_-dt);}
        else if(posture_==3)set_posture(1);
        if(control_.active==ControlEffect::Fear){auto away=position-controlOrigin_;away.y=0;velocity=away.unit()*settings_.MoveSpeed;}
        if(control_.active==ControlEffect::Knockback)velocity=controlVelocity_;
        if(control_.active==ControlEffect::Pull){auto toward=controlOrigin_-position;toward.y=0;velocity=toward.unit()*std::min(16.,std::max(0.,toward.length()-1)/dt);}
        tetherCooldown_=std::max(0.,tetherCooldown_-dt);
        if(control_.active==ControlEffect::Tether){
            auto offset=position-controlOrigin_;offset.y=0;
            if(offset.length()>6 && tetherPull_<=0 && tetherCooldown_<=0){tetherPull_=.5;tetherCooldown_=.5;grapple_.detach();grapple_.held=false;grappleFalling_=false;mantleRemaining_=0;}
            if(tetherPull_>0){tetherPull_=std::max(0.,tetherPull_-dt);velocity=offset.unit()*-std::min(16.,std::max(0.,offset.length()-5.5)/dt);}
        }else tetherPull_=0;
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        if(settings_.Collision){
            if(!controller_)controller_=std::make_unique<AdventureController>(playScene_,position);
            bool forced=false;
            if(launchAirborne_){controlVelocity_.y-=9.81*dt;velocity=controlVelocity_;forced=true;}
            if(grapple_.state!=GrappleMotion::State::Free){
                bool found=grappleTarget_=="@terrain";
                for(const auto& object:playScene_.Characters)if(object.Id==grappleTarget_ && !rules_.removed.contains(object.Id) && (!combatActors_.contains(object.Id) || combatActors_.at(object.Id).health>0)){
                    auto moved=object;if(!object.Route.empty()){auto at=object.at(seconds_);moved.X=at.X;moved.Z=at.Z;}
                    grapple_.move_anchor(world_point(moved,grappleOffset_),dt);grapple_.normal=(world_point(moved,grappleOffset_+grappleLocalNormal_)-world_point(moved,grappleOffset_)).unit();if(grapple_.state==GrappleMotion::State::Free)grappleFalling_=true;found=true;break;
                }
                if(!found){grapple_.detach();grappleFalling_=true;}
                if(grapple_.state==GrappleMotion::State::Wall && forward>0 && !control_.blocks_mobility()){
                    for(const auto& object:playScene_.Characters)if(object.Id==grappleTarget_){
                        auto moved=object;if(!object.Route.empty()){auto at=object.at(seconds_);moved.X=at.X;moved.Z=at.Z;}auto bounds=object_bounds(moved);
                        if(position.y-1.7>=bounds.high.y+.04 && controller_->traverse(position,grapple_.normal*(-.8*control_.movement_factor())+Vec3{0,.2,0})){mantleRemaining_=.4;grapple_.velocity=grapple_.normal*(-2*control_.movement_factor())+Vec3{0,.5,0};grapple_.detach();grappleFalling_=true;break;}
                    }
                }

                if(grapple_.state!=GrappleMotion::State::Free){
                    Vec3 steer{right*std::cos(yaw),forward,right*std::sin(yaw)};
                    if(grapple_.state==GrappleMotion::State::Swing)steer=velocity*(1./std::max(1.,moveSpeed));
                    if(control_.blocks_mobility())steer={};
                    bool hookFlying=grapple_.flying();auto grappleVelocity=grapple_.step(position,steer,dt,control_.movement_factor());if(!hookFlying){velocity=grappleVelocity;forced=true;}
                    if(grapple_.state==GrappleMotion::State::Wall && grapple_.normal.y>.5){grapple_.detach();grappleFalling_=true;}
                }
            }
            if(grappleFalling_ && !forced){
                if(mantleRemaining_>0)mantleRemaining_=std::max(0.,mantleRemaining_-dt);else grapple_.velocity.y-=9.81*dt;velocity=grapple_.velocity;forced=true;
            }
            auto before=position;
            {ScopedTiming timer{lastTiming.physicsMs};position=controller_->step(position,velocity,dt,jumpRequested_,rulesEnabled_?seconds_:-1,rules_.removed,forced);}
            presentationVelocity_=(position-before)*(1./dt);
            if(launchAirborne_ && controller_->grounded && controlVelocity_.y<=0){launchAirborne_=false;control_.remaining=1;controlVelocity_={};++impactSerial_;}
            if(grappleFalling_ && controller_->grounded && velocity.y<=0){grappleFalling_=false;grapple_.velocity={};}
            if(grapple_.state==GrappleMotion::State::Pulling && !grapple_.flying() && forced && velocity.length()>.1 && (position-before).length()<velocity.length()*dt*.1){grapple_.detach();grappleFalling_=true;}

            jumpRequested_=false;if(rulesEnabled_)seconds_+=dt;
            for(const auto& object:playScene_.Characters)if(!object.Route.empty()){
                auto moved=object;auto at=object.at(seconds_);moved.X=at.X;moved.Z=at.Z;
                interactionBounds_[object.Id]=object_bounds(moved);
            }
            update_rules();return;
        }
#endif
        jumpRequested_=false;if(rulesEnabled_)seconds_+=dt;
        presentationVelocity_={};
        if(right==0 && forward==0 && dodgeFraction==0){update_rules();return;}
        const auto presentationStart=position;
        Vec3 delta=velocity*dt;
        // Bounded substeps prevent tunnelling through thin walls at high speed.
        int steps=std::max(1,int(std::ceil(delta.length()/std::max(.01,settings_.PlayerRadius*.5))));
        delta=delta*(1./steps);
        for(int i=0;i<steps;i++){
            auto next=position;next.x+=delta.x;if(!blocked(next))position.x=next.x;
            next=position;next.z+=delta.z;if(!blocked(next))position.z=next.z;
            if(terrain_)position.y=terrain_->height_at(position.x,position.z)+1.7;
        }
        presentationVelocity_=(position-presentationStart)*(1./dt);
        update_rules();
    }
    std::string interact(){
        if(!rulesEnabled_)return "Start Play to use gameplay interactions.";
        if(health_<=0 || control_.blocks_basic())return "Cannot interact right now.";
        if(finished)return "Adventure complete. Reset to play again.";
        size_t found=playScene_.Characters.size();double nearest=2.5;
        for(size_t i=0;i<playScene_.Characters.size();i++){
            const auto& object=playScene_.Characters[i];
            const auto& b=interactionBounds_.at(object.Id);
            // A ray/box test chooses the first visible surface, so walls block interaction.
            double entryDistance=0,exitDistance=2.5;auto ray=direction();
            auto axis=[&](double origin,double d,double low,double high){
                if(std::abs(d)<1e-9)return origin>=low && origin<=high;
                double a=(low-origin)/d,z=(high-origin)/d;if(a>z)std::swap(a,z);
                entryDistance=std::max(entryDistance,a);exitDistance=std::min(exitDistance,z);return entryDistance<=exitDistance;
            };
            if(axis(position.x,ray.x,b.low.x,b.high.x)&&axis(position.y,ray.y,b.low.y,b.high.y)&&
               axis(position.z,ray.z,b.low.z,b.high.z)&&entryDistance<nearest){nearest=entryDistance;found=i;}
        }
        if(found==playScene_.Characters.size())return "Look at a nearby object, then press F (or E).";
        auto object=playScene_.Characters[found];
        // Existing key/door/exit roles are compatibility adapters. Authored
        // events and legacy interactions share the same action/state owner.
        const bool activated=rulesEnabled_ && rules_.emit("interact",object.Id,playScene_);
        finished=rules_.finished;
        if(finished || rules_.removed.contains(object.Id))return rules_.message;
        if(object.GameplayRole=="none")return activated ? rules_.message : "This object has no interaction.";
        const std::string effect=object.GameplayRole=="key"?"item-collected":object.GameplayRole=="door"?"object-opened":object.GameplayRole=="breakable"?"object-broken":"";
        for(const auto& rule:playScene_.Rules)if(rule.Enabled && rule.Event==effect && (rule.Subject.empty() || rule.Subject==(effect=="item-collected"?object.GameplayKey:object.Id)) && !rules_.quest_rule_available(rule.Id,playScene_))return "Finish the current quest step before using this object.";
        if(object.GameplayRole=="key") {
            rules_.remove_object(playScene_,object.Id);
            rules_.collect(object.GameplayKey,playScene_);finished=rules_.finished;
            return rules_.message.empty() ? "Collected key: "+object.GameplayKey : rules_.message;
        }
        if(object.GameplayRole=="breakable"){
            rules_.remove_object(playScene_,object.Id);rules_.emit("object-broken",object.Id,playScene_);finished=rules_.finished;
            return rules_.message.empty()?"Broken: "+object.Name:rules_.message;
        }
        if(!object.GameplayKey.empty() && !rules_.items.contains(object.GameplayKey))return "Requires key: "+object.GameplayKey;
        rules_.apply({object.GameplayRole=="exit" ? "finish-game" : "open-door",object.Id},playScene_);
        if(object.GameplayRole=="door" && rules_.removed.contains(object.Id)){auto message=rules_.message;rules_.emit("object-opened",object.Id,playScene_);if(rules_.message.empty())rules_.message=message;}
        finished=rules_.finished;return rules_.message.empty()?"Opened: "+object.Name:rules_.message;
    }
    Vec3 direction() const {return {std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)};}
};
inline std::vector<std::string> adventure_readiness(const SceneData& scene){
    std::vector<std::string> findings;
    if(!scene.Game){findings.push_back("Enable adventure controls to set up a player.");return findings;}
    std::unordered_set<std::string> keys;bool exit=false;
    for(const auto& object:scene.Characters)if(object.GameplayRole=="key")keys.insert(object.GameplayKey);
    for(const auto& object:scene.Characters){
        if(object.GameplayRole=="exit")exit=true;
        if((object.GameplayRole=="door" || object.GameplayRole=="exit") && !object.GameplayKey.empty() && !keys.contains(object.GameplayKey))
            findings.push_back(object.Name+": no collectible key named "+object.GameplayKey+".");
        if(object.Dynamic)
            findings.push_back(object.Name+": dynamic objects have no player collision in this controller; use static objects or a non-dynamic route platform.");
    }
    for(const auto& rule:scene.Rules)if(rule.Enabled) {
        for(const auto& action:rule.Actions) {
            if(action.Type=="finish-game")exit=true;
            if(action.Type=="open-door" && std::none_of(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==action.Target && c.GameplayRole=="door";}))
                findings.push_back(rule.Name+": linked door is missing or is not a door.");
        }
        for(const auto& condition:rule.Conditions)if(condition.Type=="has-key" && !keys.contains(condition.Value))
            findings.push_back(rule.Name+": no collectible item named "+condition.Value+".");
        if(rule.Event=="interact" && !rule.Subject.empty() && std::none_of(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==rule.Subject;}))
            findings.push_back(rule.Name+": interaction object is missing.");
    }
    if(!exit)findings.push_back("No finish / exit object: players can explore, but cannot complete this adventure.");
    AdventurePlayer player;player.start(scene);
    if(player.standing_inside_solid())findings.push_back("The player starts inside a solid object. Move the start position or the object.");
    return findings;
}
}
