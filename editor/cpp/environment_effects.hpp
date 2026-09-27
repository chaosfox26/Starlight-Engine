#pragma once
#include "environment_events.hpp"
#include "particle_physics.hpp"
#include "flow.hpp"
#include "lightning_physics.hpp"
#include "../../deep/engine/chaos_lightning.h"
#include <unordered_map>

namespace starlight::editor {
inline std::vector<std::array<float,4>> smoke_sources(const SceneData& scene,double time,const TerrainFlow* flow){
    std::vector<std::array<float,4>> sources;
    for(auto c:scene.Characters){bool fire=c.Shape=="fire";if(c.Shape!="craxis-smoke"&&!fire)continue;if(fire&&c.ParticleRate<=0)continue;
        c=route_pose(c,time);auto base=world_point(c,{0,.12,0});if(fire&&flow){auto liquid=flow->liquid_at(base.x,base.z);if(liquid.second==1000&&base.y<=liquid.first)continue;}
        auto position=world_point(c,{0,fire?.7:.5,0});double radius=std::max({c.Width,c.Height,c.Depth})*c.Size*(fire?.8:1.5);
        sources.push_back({float(position.x),float(position.y),float(position.z),float(radius)});if(sources.size()==16)break;
    }
    return sources;
}
inline std::vector<std::array<float,12>> environment_lights(const SceneData& scene,double time,const TerrainFlow* flow){
    std::vector<std::array<float,12>> lights;
    for(auto c:scene.Characters){auto kind=c.Shape.value_or("");if(kind!="fire"&&kind!="light"&&kind!="chaos-lightning"&&kind!="chaos-rock"&&kind!="light-shard")continue;
        c=route_pose(c,time);if(kind=="fire"){if(c.ParticleRate<=0)continue;if(flow){auto base=world_point(c,{0,.12,0});auto liquid=flow->liquid_at(base.x,base.z);if(liquid.second==1000&&base.y<=liquid.first)continue;}}
        auto target=world_point(c,kind=="chaos-lightning"?Vec3{c.TargetX,c.TargetY,c.TargetZ}:Vec3{0,.7,0});
        if(kind=="chaos-lightning"&&!c.LightningTargetId.empty()){auto other=std::find_if(scene.Characters.begin(),scene.Characters.end(),[&](const auto& object){return object.Id==c.LightningTargetId;});if(other==scene.Characters.end())continue;target=world_point(route_pose(*other,time),{c.TargetX,c.TargetY,c.TargetZ});}
        auto direction=(world_point(c,{0,-1,0})-world_point(c,{})).unit();auto color=rgb(kind=="chaos-lightning"||kind=="chaos-rock"?"#C42DFF":c.Color)*c.LightIntensity;
        lights.push_back({float(target.x),float(target.y),float(target.z),float(c.LightRange),float(direction.x),float(direction.y),float(direction.z),float(kind=="chaos-lightning"?-3:c.LightCone>=180?-2:std::cos(c.LightCone*3.141592653589793/360)),float(color.x),float(color.y),float(color.z),float(c.LightCone>=180?-1:std::cos(c.LightCone*3.141592653589793/450))});if(lights.size()==32)break;
    }
    return lights;
}
// Runtime-only effects. Authored transforms are never overwritten by simulation.
class EnvironmentEffects {
    struct Strike { Vec3 source{},target{};double age=0;uint32_t seed=1;int64_t cycle=-1;bool contact=false,emitted=false;std::vector<LightningSegment> segments; };
    std::unordered_map<std::string,Strike> strikes_;
    std::vector<PhysicalParticle> sparks_;
    struct Flame {PhysicalParticle particle;Vec3 color;double size;double lift=14;Vec3 wind{.3,0,.15};};
    struct Emitter {double remainder=0;Vec3 previous{};bool initialized=false;};
    std::unordered_map<std::string,Emitter> emitters_;
    std::vector<Flame> flames_;
    struct Droplet {PhysicalParticle particle;bool lava=false;};
    std::vector<Droplet> droplets_;
    struct GrassState {std::vector<MeshPart> source,rendered;std::vector<Vec3> displacement,velocity;};
    std::unordered_map<std::string,GrassState> grass_;

    uint32_t random_=8241;uint64_t particleSerial_=0;
    double time_=0;uint64_t epoch_=0,lightningSerial_=0;std::vector<LightningSoundEvent> lightningSounds_;
    double random(){random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;return double(random_&65535)/32767.5-1;}
    static bool solid(const Character& c){
        auto k=c.Shape.value_or("");
        return k!="grass" && k!="fire" && k!="light" && k!="lava" && k!="chaos-lightning" && k!="light-shard" && k!="craxis-smoke";
    }
  public:
    void reset(){++epoch_;lightningSerial_=0;lightningSounds_.clear();strikes_.clear();sparks_.clear();emitters_.clear();flames_.clear();droplets_.clear();grass_.clear();random_=8241;particleSerial_=0;time_=0;}
    bool active(const SceneData& scene)const {
        return !sparks_.empty() || !flames_.empty() || !droplets_.empty() || std::any_of(scene.Characters.begin(),scene.Characters.end(),[](const auto& c){return c.Shape=="chaos-lightning" || c.Shape=="fire" || c.Shape=="grass";});
    }
    uint64_t epoch()const{return epoch_;}
    uint64_t lightning_serial()const{return lightningSerial_;}
    const auto& lightning_sounds()const{return lightningSounds_;}
    size_t spark_count()const{return sparks_.size();}
    size_t flame_count()const{return flames_.size();}
    size_t splash_count()const{return droplets_.size();}
    void splash(const LiquidSplash& splash){
        int count=std::clamp(int(splash.strength*16),4,32);
        for(int i=0;i<count&&droplets_.size()<1024;++i){PhysicalParticle p;p.motionId=++particleSerial_;p.position=splash.position+Vec3{random()*.12,.04,random()*.12};p.velocity=Vec3{random()*splash.strength,1+splash.strength*2,random()*splash.strength}+Vec3{splash.velocity.x*.25,0,splash.velocity.z*.25};p.radius=.02;p.drag=splash.lava?1.:.2;p.restitution=.05;p.lifetime=splash.lava?.7:1.4;droplets_.push_back({p,splash.lava});}
    }
    std::optional<Vec3> contact(std::string_view id)const{
        auto it=strikes_.find(std::string(id));if(it==strikes_.end() || !it->second.contact)return {};return it->second.target;
    }
    void step(const SceneData& scene,double dt,double routeTime,TerrainFlow* liquids=nullptr,std::optional<Bounds> player={}){
        if(!std::isfinite(dt) || dt<0 || dt>.05)throw std::runtime_error("Invalid environmental effects step");
        time_+=dt;
        std::vector<Bounds> obstacles;
        for(auto c:scene.Characters)if(solid(c)){c=route_pose(c,routeTime);obstacles.push_back(object_bounds(c));}
        if(player)obstacles.push_back(*player);
        auto terrain=scene.Terrain?&*scene.Terrain:nullptr;
        std::erase_if(grass_,[&](const auto& entry){return std::none_of(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==entry.first&&c.Shape=="grass";});});
        for(auto c:scene.Characters)if(c.Shape=="grass"){
            c=route_pose(c,routeTime);auto [it,fresh]=grass_.try_emplace(c.Id);auto& state=it->second;
            if(fresh){state.source=object_parts(c);size_t count=0;for(const auto& part:state.source)count+=part.mesh.vertices.size();state.displacement.resize(count);state.velocity.resize(count);}
            state.rendered=state.source;size_t index=0;uint64_t identity=render_identity(c.Id,0);
            for(auto& part:state.rendered){part.color=rgb(c.Color);for(auto& vertex:part.mesh.vertices){auto local=vertex.position,rest=world_point(c,local);double bend=std::clamp(local.y/.6,0.,1.);bend*=bend;
                Vec3 target{bend*(.10*std::sin(routeTime*1.7+rest.x*.4+rest.z*.25)+.025*std::sin(routeTime*4.1+rest.z)),0,bend*.07*std::cos(routeTime*1.3+rest.z*.3)};
                for(const auto& box:obstacles)if(rest.y>=box.low.y-.1&&rest.y<=box.high.y+.1&&rest.x>box.low.x-.2&&rest.x<box.high.x+.2&&rest.z>box.low.z-.2&&rest.z<box.high.z+.2){
                    double distances[]={rest.x-(box.low.x-.2),box.high.x+.2-rest.x,rest.z-(box.low.z-.2),box.high.z+.2-rest.z};Vec3 directions[]={{-1,0,0},{1,0,0},{0,0,-1},{0,0,1}};int face=int(std::min_element(std::begin(distances),std::end(distances))-std::begin(distances));target=target+directions[face]*(std::min(.8,distances[face])*bend);
                }
                // Critically damped recovery uses elapsed time, preserving a rooted plant
                // while wind and contacting bodies change its bend target.
                constexpr double omega=12;auto error=state.displacement[index]-target,carry=state.velocity[index]+error*omega;double decay=std::exp(-omega*dt);state.displacement[index]=target+(error+carry*dt)*decay;state.velocity[index]=(state.velocity[index]-carry*(omega*dt))*decay;
                vertex.position=rest+state.displacement[index];vertex.kind=1.25f;vertex.motionIdentity=vertex_motion_identity(12,identity,index++);
            }part.mesh.normals();}
        }

        for(auto& drop:droplets_){step_particle(drop.particle,dt,terrain,obstacles,{.3,0,.15});if(liquids&&drop.particle.velocity.y<0){auto [height,density]=liquids->liquid_at(drop.particle.position.x,drop.particle.position.z);if(density>0&&drop.particle.position.y<=height){liquids->impulse(drop.particle.position.x,drop.particle.position.z,.015);drop.particle.age=drop.particle.lifetime;}}}
        std::erase_if(droplets_,[](const auto& drop){return drop.particle.age>=drop.particle.lifetime;});
        for(auto& p:sparks_)step_particle(p,dt,terrain,obstacles,{.3,0,.15});
        std::erase_if(sparks_,[](const auto& p){return p.age>=p.lifetime;});
        auto submerged=[&](Vec3 position){if(!liquids)return false;auto [height,density]=liquids->liquid_at(position.x,position.z);return density==1000&&position.y<=height;};
        for(auto& flame:flames_){if(submerged(flame.particle.position)){flame.particle.age=flame.particle.lifetime;continue;}step_particle(flame.particle,dt,terrain,obstacles,flame.wind,{0,flame.lift-9.81,0});if(submerged(flame.particle.position))flame.particle.age=flame.particle.lifetime;}
        std::erase_if(flames_,[](const auto& flame){return flame.particle.age>=flame.particle.lifetime;});
        std::erase_if(emitters_,[&](const auto& entry){return std::none_of(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==entry.first&&c.Shape=="fire";});});
        for(auto c:scene.Characters)if(c.Shape=="fire"){
            c=route_pose(c,routeTime);auto origin=world_point(c,{0,.12,0});auto& emitter=emitters_[c.Id];Vec3 inherited{};
            if(emitter.initialized&&dt>0){inherited=(origin-emitter.previous)/dt;if(inherited.length()>20)inherited={};}emitter.previous=origin;emitter.initialized=true;emitter.remainder+=dt*c.ParticleRate;
            if(submerged(origin)){emitter.remainder=0;continue;}
            int count=int(emitter.remainder);emitter.remainder-=count;
            for(int i=0;i<count&&flames_.size()<1024;++i){PhysicalParticle p;p.motionId=++particleSerial_;p.position=origin+Vec3{random()*.18*c.Size,0,random()*.18*c.Size};p.velocity=inherited+Vec3{random()*.3,1.2+std::abs(random()),random()*.3};p.radius=.025;p.drag=c.ParticleDrag;p.restitution=.05;p.lifetime=c.ParticleLifetime*(.65+std::abs(random())*.5);flames_.push_back({p,rgb(c.Color),.12*c.Size,c.ParticleLift,{c.ParticleWindX,c.ParticleWindY,c.ParticleWindZ}});}
        }

        std::erase_if(strikes_,[&](const auto& item){return std::none_of(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==item.first && c.Shape=="chaos-lightning";});});
        for(auto c:scene.Characters)if(c.Shape=="chaos-lightning"){
            c=route_pose(c,routeTime);
            auto segments=trace_lightning(c,scene,routeTime,player);if(segments.empty()){strikes_.erase(c.Id);continue;}
            auto [it,fresh]=strikes_.try_emplace(c.Id);auto& strike=it->second;strike.age=std::fmod(std::max(0.,routeTime),.22);int64_t cycle=int64_t(std::floor(std::max(0.,routeTime)/.22));
            strike.segments=std::move(segments);const auto& primary=strike.segments.front();strike.source=primary.source;strike.target=primary.target;strike.contact=primary.contact;auto hitNormal=primary.normal;
            // A fresh discharge changes branch topology and emits one contact burst.
            if(fresh || strike.cycle!=cycle){
                strike.cycle=cycle;random();strike.seed=random_;strike.emitted=false;
            }
            if(!strike.emitted && strike.age>=.025){
                strike.emitted=true;lightningSounds_.push_back({++lightningSerial_,strike.target,routeTime});if(lightningSounds_.size()>128)lightningSounds_.erase(lightningSounds_.begin());
                for(const auto& arc:strike.segments)if(arc.contact)for(int j=0;j<12 && sparks_.size()<2048;++j){
                    PhysicalParticle p;p.motionId=++particleSerial_;p.position=arc.target+arc.normal*.035;
                    p.velocity=Vec3{random()*2.5,std::abs(random())*3,random()*2.5}+arc.normal*1.5;
                    p.radius=.015;p.drag=.6;p.restitution=.4;p.lifetime=.35+.45*std::abs(random());sparks_.push_back(p);
                }
            }
        }
    }
    std::vector<MeshPart> geometry(const SceneData& scene)const{
        std::vector<MeshPart> result;
        for(const auto& c:scene.Characters)if(auto it=grass_.find(c.Id);it!=grass_.end())result.insert(result.end(),it->second.rendered.begin(),it->second.rendered.end());
        for(const auto& c:scene.Characters)if(c.Shape=="chaos-lightning"){
            auto it=strikes_.find(c.Id);if(it==strikes_.end())continue;const auto& strike=it->second;
            for(size_t segment=0;segment<strike.segments.size();++segment){const auto& arc=strike.segments[segment];if(arc.path.size()<2)continue;
                double length=0;for(size_t i=1;i<arc.path.size();++i)length+=(arc.path[i]-arc.path[i-1]).length();double remaining=length*std::clamp(strike.age/.025,0.,1.);
                MeshPart part;part.color={1,1,1};part.emissive=true;uint64_t vertexIndex=0,arcId=render_identity(c.Id,strike.seed+uint32_t(segment));
                for(size_t i=1;i<arc.path.size()&&remaining>1e-9;++i){auto a=arc.path[i-1],delta=arc.path[i]-a;double span=delta.length();if(span<1e-9)continue;auto b=a+delta*std::min(1.,remaining/span);remaining-=span;double width=arc.branch?.017:.035;
                    for(auto axis:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}}){auto side=delta.unit().cross(axis);if(side.length()<1e-6)continue;side=side.unit()*width;int first=int(part.mesh.vertices.size());int j=0;for(auto point:{a-side,b-side,b+side,a+side}){double anchor=std::clamp(std::min((point-arc.source).length(),(point-arc.target).length())/.35,0.,1.);part.mesh.vertices.push_back({point,{}, {j++<2?0.:1.,anchor,1},9.25f,vertex_motion_identity(2,arcId,vertexIndex++)});}part.mesh.indices.insert(part.mesh.indices.end(),{first,first+1,first+2,first,first+2,first+3});}
                }
                if(!part.mesh.indices.empty()){part.mesh.normals();result.push_back(std::move(part));}
            }
        }

        if(!flames_.empty()){
            MeshPart part;part.emissive=true;
            for(const auto& flame:flames_){const auto& p=flame.particle;uint64_t vertexIndex=0;double fade=std::clamp(1-p.age/p.lifetime,0.,1.),radius=flame.size*std::sqrt(fade);Vec3 color=(flame.color*.65+Vec3{1,.35,.02}*.35)*fade;
                for(auto side:{Vec3{radius,0,0},Vec3{0,0,radius}}){int n=int(part.mesh.vertices.size());for(auto at:{p.position-side,p.position+side,p.position+Vec3{0,radius*3,0}})part.mesh.vertices.push_back({at,{0,1,0},color,0,vertex_motion_identity(1,p.motionId,vertexIndex++)});part.mesh.indices.insert(part.mesh.indices.end(),{n,n+1,n+2,n+2,n+1,n});}
            }part.mesh.normals();result.push_back(std::move(part));
        }
        for(bool lava:{false,true}){
            MeshPart part;part.color=lava?Vec3{1,.3,.03}:Vec3{.15,.55,.9};part.emissive=lava;
            for(const auto& drop:droplets_)if(drop.lava==lava){const auto& p=drop.particle;uint64_t vertexIndex=0;double r=p.radius;auto tail=p.position-p.velocity*.012;
                for(auto side:{Vec3{r,0,0},Vec3{0,0,r}}){int n=int(part.mesh.vertices.size());for(auto v:{p.position-side,p.position+side,tail+side,tail-side})part.mesh.vertices.push_back({v,{}, {1,1,1},0,vertex_motion_identity(1,p.motionId,vertexIndex++)});part.mesh.indices.insert(part.mesh.indices.end(),{n,n+1,n+2,n,n+2,n+3});}
            }if(!part.mesh.indices.empty()){part.mesh.normals();result.push_back(std::move(part));}
        }
        if(!sparks_.empty()){
            MeshPart part;part.color={1,.7,.3};part.emissive=true;
            for(const auto& p:sparks_){uint64_t vertexIndex=0;
                auto a=p.position,b=p.position-p.velocity*.015;double r=.015*(1-p.age/p.lifetime);
                for(auto side:{Vec3{r,0,0},Vec3{0,r,0},Vec3{0,0,r}}){int n=int(part.mesh.vertices.size());for(auto v:{a-side,a+side,b+side,b-side})part.mesh.vertices.push_back({v,{}, {1,1,1},0,vertex_motion_identity(1,p.motionId,vertexIndex++)});part.mesh.indices.insert(part.mesh.indices.end(),{n,n+1,n+2,n,n+2,n+3});}
            }
            part.mesh.normals();result.push_back(std::move(part));
        }
        return result;
    }
};
}
