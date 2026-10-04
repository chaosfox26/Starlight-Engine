#include "terrain_collision.hpp"
#include "adventure_controller.hpp"
#include "object_cache.hpp"
#include <atomic>
#include <bit>
#include <mutex>
#include <algorithm>
#include "body_constraints.hpp"
#include <DirectXMath.h>
#include <numbers>
namespace {
extern "C" {
__declspec(dllimport) int slp_body_impulse(void*,uint32_t,float,float,float,float,float,float);
__declspec(dllimport) int slp_body_drive(void*,uint32_t,float,float,float,int);
__declspec(dllimport) int slp_body_current(void*,uint32_t,float,float);
__declspec(dllimport) int slp_body_liquid(void*,uint32_t,float,float,float);
__declspec(dllimport) int slp_body_material(void*,uint32_t,float,float);
__declspec(dllimport) void* slp_create();
__declspec(dllimport) void slp_destroy(void*);
__declspec(dllimport) const char* slp_error();
__declspec(dllimport) int slp_terrain(void*,const float*);
__declspec(dllimport) uint32_t slp_add(void*,const float*,int,const uint32_t*,int,int,float,float,float,float,float,float,float,float);
__declspec(dllimport) void* slp_hull_create(const float*,int,float);
__declspec(dllimport) void slp_hull_release(void*);
__declspec(dllimport) int slp_hull_info(void*,float*);
__declspec(dllimport) int slp_hull_points(void*,float*,int);
__declspec(dllimport) void slp_hull_stats(double*);
__declspec(dllimport) int slp_hull_last_box(char*,int);
__declspec(dllimport) void slp_hull_reset_stats();
__declspec(dllimport) void slp_shape_cache_stats(double*);
__declspec(dllimport) uint32_t slp_add_hull(void*,void*,float,float,float,float,float,float,float);
__declspec(dllimport) int slp_character_current(void*,float,float);
__declspec(dllimport) int slp_character_liquid(void*,float,float);
__declspec(dllimport) int slp_character_start(void*,float,float,float,float);
__declspec(dllimport) int slp_character_height(void*,float);
__declspec(dllimport) int slp_character_traverse(void*,float,float,float,float,float,float);
__declspec(dllimport) int slp_character_step(void*,float,float,float,float,float,float,int,float*);
__declspec(dllimport) int slp_character_motion(void*,float,float,float,float,float,float,float,float*);
__declspec(dllimport) void slp_character_remove(void*,uint32_t);
__declspec(dllimport) void slp_character_actor(void*,uint32_t,float,float,float,float,float,float);
__declspec(dllimport) void slp_character_platform(void*,uint32_t,float,float,float,float);
__declspec(dllimport) void slp_body_velocity(void*,uint32_t,float,float,float,float,float,float);
__declspec(dllimport) int slp_body_motion(void*,uint32_t,float,float,float,float*);
__declspec(dllimport) int slp_body_pose(void*,uint32_t,float*);
}
void checked(int result){if(!result)throw std::runtime_error(slp_error());}
}
namespace starlight::editor {
static std::atomic<bool>& static_sharing(){static std::atomic<bool> on=[]{char v[8]{};return !(GetEnvironmentVariableA("STARLIGHT_SHARE_STATIC_SHAPES",v,8)&&v[0]=='0');}();return on;}
void share_static_shapes(bool on){static_sharing()=on;}
bool share_static_shapes(){return static_sharing();}
void AdventureController::terrain(const TerrainData& value){
    value.validate();
    if(!value.Volumes.empty())checked(install_terrain_volume_collision(world_,value));
    else {std::vector<float> heights(value.Heights.begin(),value.Heights.end());checked(slp_terrain(world_,heights.data()));}
}
AdventureController::AdventureController(const SceneData& scene,Vec3 eye){
    world_=slp_create();if(!world_)throw std::runtime_error(slp_error());
    try {
        auto terrain=scene.Terrain.value_or(TerrainData{});
        if(!terrain.Volumes.empty())checked(install_terrain_volume_collision(world_,terrain));
        else {std::vector<float> heights(terrain.Heights.begin(),terrain.Heights.end());checked(slp_terrain(world_,heights.data()));}
        for(const auto& object:scene.Characters){
            auto kind=object.Shape.value_or("");
            if(object.GameplayRole=="key" || object.GameplayRole=="exit" || kind=="light" || kind=="fire" || kind=="craxis-smoke" || kind=="grass" || kind=="lava" || kind=="chaos-lightning")continue;
            // The collision mesh comes from the shared object cache (line of sight, liquid obstacles and the loading step use the same one).
            const std::shared_ptr<const Mesh> held=ObjectCache::shared().mesh(object);
            const Mesh& mesh=*held;std::vector<float> points;points.reserve(mesh.vertices.size()*3);
            bool moving=(!object.Route.empty() || object.CombatPursuit || object.CombatHealth>0) && !object.Dynamic;
            const bool ownSpace=object.Dynamic||(!moving&&!object.PhysicsRotation&&share_static_shapes());
            for(const auto& v:mesh.vertices){auto p=ownSpace?Vec3{v.position.x*object.Width*object.Size,v.position.y*object.Height*object.Size,v.position.z*object.Depth*object.Size}:world_point(object,v.position)-Vec3{object.X,object.Y,object.Z};points.insert(points.end(),{float(p.x),float(p.y),float(p.z)});}
            std::vector<uint32_t> indices(mesh.indices.begin(),mesh.indices.end());
            DirectX::XMFLOAT4 rotation{0,0,0,1};
            if(ownSpace){using namespace DirectX;float rad=float(std::numbers::pi/180);auto x=XMQuaternionRotationAxis(XMVectorSet(1,0,0,0),float(object.RotationX)*rad),y=XMQuaternionRotationAxis(XMVectorSet(0,1,0,0),float(object.RotationY)*rad),z=XMQuaternionRotationAxis(XMVectorSet(0,0,1,0),float(object.RotationZ)*rad);XMStoreFloat4(&rotation,XMQuaternionMultiply(XMQuaternionMultiply(x,y),z));}
            auto id=slp_add(world_,points.data(),int(mesh.vertices.size()),indices.data(),int(indices.size()),moving||object.Dynamic?1:0,float(object.Density),float(object.X),float(object.Y),float(object.Z),rotation.x,rotation.y,rotation.z,rotation.w);
            if(id==UINT32_MAX)throw std::runtime_error(slp_error());
            checked(slp_body_material(world_,id,float(object.Friction),float(object.Restitution)));
            bodies_.emplace(object.Id,id);baseAngles_.emplace(object.Id,std::pair{object.RotationY,object.RotationZ});
            if(object.Dynamic){slp_body_velocity(world_,id,float(object.VelocityX),float(object.VelocityY),float(object.VelocityZ),float(object.AngularVelocityX*.0174532925199433),float(object.AngularVelocityY*.0174532925199433),float(object.AngularVelocityZ*.0174532925199433));dynamic_.insert(object.Id);liquidBodies_[id]=object.Density;}
            if(moving){if(!object.Route.empty())platforms_.push_back(object);slp_character_platform(world_,id,float(object.X),float(object.Y),float(object.Z),1.f/60);}
        }
        create_body_constraints(world_,scene,bodies_);
        checked(slp_character_start(world_,float(scene.Game->PlayerRadius),float(eye.x),float(eye.y-1.7),float(eye.z)));
    }catch(...){slp_destroy(world_);world_=nullptr;throw;}
}
void AdventureController::move_actor(const std::string& id,Vec3 position,double dt,double yawDegrees){
    if(auto it=bodies_.find(id);it!=bodies_.end()){if(dynamic_.contains(id)){if(dt<=0)return;float pose[7]{};checked(slp_body_pose(world_,it->second,pose));auto delta=position-Vec3{pose[0],pose[1],pose[2]};auto drive=delta*(1./dt);checked(slp_body_drive(world_,it->second,float(drive.x),float(drive.y),float(drive.z),std::abs(delta.y)>1e-5));return;}auto angles=baseAngles_.at(id);slp_character_actor(world_,it->second,float(position.x),float(position.y),float(position.z),float((yawDegrees-angles.first)*.0174532925199433),float(angles.second*.0174532925199433),float(std::max(dt,1./120.)));}
}
AdventureController::~AdventureController(){if(world_)slp_destroy(world_);}
void AdventureController::impulse_body(uint32_t id,Vec3 at,Vec3 momentum){checked(slp_body_impulse(world_,id,float(at.x),float(at.y),float(at.z),float(momentum.x),float(momentum.y),float(momentum.z)));}
void AdventureController::impulse(const std::string& id,Vec3 at,Vec3 momentum){if(auto body=bodies_.find(id);body!=bodies_.end())checked(slp_body_impulse(world_,body->second,float(at.x),float(at.y),float(at.z),float(momentum.x),float(momentum.y),float(momentum.z)));}
void AdventureController::sync_dynamic(SceneData& scene){
    for(auto& c:scene.Characters)if(dynamic_.contains(c.Id))if(auto body=bodies_.find(c.Id);body!=bodies_.end()){
        float pose[7]{};checked(slp_body_pose(world_,body->second,pose));c.X=pose[0];c.Y=pose[1];c.Z=pose[2];c.PhysicsRotation=std::array<double,4>{pose[3],pose[4],pose[5],pose[6]};c.Route.clear();update_rope_path(world_,body->second,c,false);
    }
}
std::pair<Vec3,Vec3> AdventureController::body_motion(const std::string& name,Vec3 point)const{auto found=bodies_.find(name);if(found==bodies_.end())return {};float values[6]{};checked(slp_body_motion(world_,found->second,float(point.x),float(point.y),float(point.z),values));return {{values[0],values[1],values[2]},{values[3],values[4],values[5]}};}
// Fragment meshes store each triangle's corners separately, so most points repeat. A convex hull depends only on the
// distinct points (first occurrences keep their order; verified to build identical hulls), and builds several times faster.
static std::vector<float> debris_points(const Mesh& mesh){
    std::vector<float> points;points.reserve(mesh.vertices.size()*3);
    {
        std::vector<uint32_t> table(std::bit_ceil(std::max<size_t>(16,mesh.vertices.size()*2)),UINT32_MAX);
        const size_t mask=table.size()-1;
        auto bits=[](float value){return std::bit_cast<uint32_t>(value==0.f?0.f:value);};
        for(const auto& v:mesh.vertices){
            const float x=float(v.position.x),y=float(v.position.y),z=float(v.position.z);
            uint64_t h=(uint64_t(bits(x))*0x9E3779B97F4A7C15ull)^(uint64_t(bits(y))*0xC2B2AE3D27D4EB4Full)^(uint64_t(bits(z))*0x165667B19E3779F9ull);h^=h>>29;
            size_t slot=size_t(h)&mask;bool found=false;
            for(;table[slot]!=UINT32_MAX;slot=(slot+1)&mask){const float* q=&points[size_t(table[slot])*3];if(q[0]==x&&q[1]==y&&q[2]==z){found=true;break;}}
            if(!found){table[slot]=uint32_t(points.size()/3);points.insert(points.end(),{x,y,z});}
        }
        // Too few distinct points: keep the original input so failure behaviour is unchanged.
        if(points.size()/3<4){points.clear();for(const auto& v:mesh.vertices)points.insert(points.end(),{float(v.position.x),float(v.position.y),float(v.position.z)});}
    }
    return points;
}
// Builds the collision hull of one fragment without touching any physics world, so it can run on a background thread.
// Returns null (and the caller falls back to the synchronous path, which reports the error) if the mesh is not acceptable.
static std::atomic<uint64_t> unbuildable_hulls{0};
static std::mutex unbuildable_lock;
static std::string unbuildable_reason;
DebrisHull build_debris_hull(const Mesh& mesh,double density){
    auto refuse=[](const char* why){++unbuildable_hulls;std::lock_guard<std::mutex> guard(unbuildable_lock);unbuildable_reason=why;return DebrisHull{};};
    if(mesh.indices.size()<3||mesh.indices.size()%3)return refuse("the piece has no complete triangle");
    auto points=debris_points(mesh);
    if(points.size()/3<4||points.size()/3>200000)return refuse("the piece has fewer than 4 or more than 200000 distinct points");
    void* handle=slp_hull_create(points.data(),int(points.size()/3),float(density));
    if(!handle)return refuse(slp_error());
    return DebrisHull(handle,[](const void* h){slp_hull_release(const_cast<void*>(h));});
}
bool hull_detail(const DebrisHull& hull,HullDetail& out){
    float v[5]{};if(!hull||!slp_hull_info(const_cast<void*>(hull.get()),v))return false;
    out.kind=int(v[0]);out.inputPoints=int(v[1]);out.hullPoints=int(v[2]);out.deviation=v[3];out.diagonal=v[4];return true;
}
std::vector<std::array<float,3>> hull_points(const DebrisHull& hull){
    std::vector<std::array<float,3>> out;if(!hull)return out;
    std::vector<float> raw(3*256);const int n=slp_hull_points(const_cast<void*>(hull.get()),raw.data(),256);
    for(int i=0;i<n;++i)out.push_back({raw[3*i],raw[3*i+1],raw[3*i+2]});
    return out;
}
HullReport hull_report(){
    double s[9]{};slp_hull_stats(s);HullReport r;
    r.requested=uint64_t(s[0]);r.exact=uint64_t(s[1]);r.exactAfterFinerRetry=uint64_t(s[2]);r.reducedToLimit=uint64_t(s[3]);r.reducedBeyondBound=uint64_t(s[4]);r.boxFallbacks=uint64_t(s[5]);r.rejected=uint64_t(s[6]);r.worstReducedRelativeDeviation=s[7];r.rebuiltFromHullVertices=uint64_t(s[8]);
    r.unbuildable=unbuildable_hulls.load();
    char buffer[512]{};if(slp_hull_last_box(buffer,512)>0)r.lastBoxFallback=buffer;
    {std::lock_guard<std::mutex> guard(unbuildable_lock);r.lastUnbuildable=unbuildable_reason;}
    return r;
}
void reset_hull_report(){slp_hull_reset_stats();unbuildable_hulls=0;std::lock_guard<std::mutex> guard(unbuildable_lock);unbuildable_reason.clear();}
ShapeCacheReport shape_cache_report(){double s[5]{};slp_shape_cache_stats(s);ShapeCacheReport r;r.shapes=size_t(s[0]);r.bytes=size_t(s[1]);r.hits=uint64_t(s[2]);r.misses=uint64_t(s[3]);r.trimmed=uint64_t(s[4]);return r;}
uint32_t AdventureController::add_debris(const Mesh& mesh,Vec3 at,Vec3 velocity,double density,double friction,double restitution,std::optional<Vec3> spin,const DebrisHull& hull,std::array<double,4> q){
    uint32_t id;
    if(hull)id=slp_add_hull(world_,const_cast<void*>(hull.get()),float(at.x),float(at.y),float(at.z),float(q[0]),float(q[1]),float(q[2]),float(q[3]));
    else {
        auto points=debris_points(mesh);
        std::vector<uint32_t> indices(mesh.indices.begin(),mesh.indices.end());
        id=slp_add(world_,points.data(),int(points.size()/3),indices.data(),int(indices.size()),1,float(density),float(at.x),float(at.y),float(at.z),float(q[0]),float(q[1]),float(q[2]),float(q[3]));
    }
    if(id==UINT32_MAX)throw std::runtime_error(slp_error());
    checked(slp_body_material(world_,id,float(friction),float(restitution)));
    liquidBodies_[id]=density;
    auto angular=spin.value_or(Vec3{velocity.z,1,-velocity.x});slp_body_velocity(world_,id,float(velocity.x),float(velocity.y),float(velocity.z),float(angular.x),float(angular.y),float(angular.z));return id;
}
void AdventureController::debris_pose(uint32_t id,Vec3& at,std::array<double,4>& rotation){float pose[7]{};checked(slp_body_pose(world_,id,pose));at={pose[0],pose[1],pose[2]};rotation={pose[3],pose[4],pose[5],pose[6]};}
void AdventureController::remove_debris(uint32_t id){liquidBodies_.erase(id);slp_character_remove(world_,id);}
bool AdventureController::height(double value){auto result=slp_character_height(world_,float(value));if(result<0)throw std::runtime_error(slp_error());return result!=0;}
bool AdventureController::traverse(Vec3 eye,Vec3 travel){auto result=slp_character_traverse(world_,float(eye.x),float(eye.y-1.7),float(eye.z),float(travel.x),float(travel.y),float(travel.z));if(result<0)throw std::runtime_error(slp_error());return result!=0;}
Vec3 AdventureController::step(Vec3 eye,Vec3 velocity,double seconds,bool jump,double time,const std::unordered_set<std::string>& removed,bool overrideMotion){
    for(const auto& id:removed)if(auto it=bodies_.find(id);it!=bodies_.end()){liquidBodies_.erase(it->second);slp_character_remove(world_,it->second);bodies_.erase(it);}
    const int count=std::max(1,int(std::ceil(seconds/(1./120))));const double dt=seconds/count;
    for(int i=0;i<count;i++){
        for(const auto& platform:platforms_)if(time>=0)if(auto body=bodies_.find(platform.Id);body!=bodies_.end()){
            auto point=platform.at(time+dt*(i+1));slp_character_actor(world_,body->second,float(point.X),float(platform.Y),float(point.Z),float(platform.route_yaw(time+dt*(i+1))),0,float(dt));
        }
        if(liquids_)for(const auto& [id,density]:liquidBodies_){float pose[7]{};checked(slp_body_pose(world_,id,pose));auto [height,liquidDensity]=liquids_->at(pose[0],pose[2]);auto current=liquids_->velocity_at(pose[0],pose[2]);checked(slp_body_current(world_,id,float(current.x),float(current.z)));if(liquidDensity>0)checked(slp_body_liquid(world_,id,float(height),float(liquidDensity/density),float(dt)));}
        auto liquid=liquids_?liquids_->at(eye.x,eye.z):std::pair<double,double>{};
        auto current=liquids_?liquids_->velocity_at(eye.x,eye.z):Vec3{};checked(slp_character_current(world_,float(current.x),float(current.z)));
        checked(slp_character_liquid(world_,float(liquid.second>0?liquid.first:0),float(liquid.second)));
        float output[4]{};
        if(overrideMotion)checked(slp_character_motion(world_,float(dt),float(eye.x),float(eye.y-1.7),float(eye.z),float(velocity.x),float(velocity.y),float(velocity.z),output));
        else checked(slp_character_step(world_,float(dt),float(eye.x),float(eye.y-1.7),float(eye.z),float(velocity.x),float(velocity.z),jump && i==0,output));
        eye={output[0],double(output[1])+1.7,output[2]};grounded=output[3]!=0;
    }
    return eye;
}
}
