#include "adventure_controller.hpp"
namespace {
extern "C" {
__declspec(dllimport) void* slp_create();
__declspec(dllimport) void slp_destroy(void*);
__declspec(dllimport) const char* slp_error();
__declspec(dllimport) int slp_terrain(void*,const float*);
__declspec(dllimport) uint32_t slp_add(void*,const float*,int,const uint32_t*,int,int,float,float,float,float,float,float,float,float);
__declspec(dllimport) int slp_character_start(void*,float,float,float,float);
__declspec(dllimport) int slp_character_height(void*,float);
__declspec(dllimport) int slp_character_traverse(void*,float,float,float,float,float,float);
__declspec(dllimport) int slp_character_step(void*,float,float,float,float,float,float,int,float*);
__declspec(dllimport) int slp_character_motion(void*,float,float,float,float,float,float,float,float*);
__declspec(dllimport) void slp_character_remove(void*,uint32_t);
__declspec(dllimport) void slp_character_actor(void*,uint32_t,float,float,float,float,float,float);
__declspec(dllimport) void slp_character_platform(void*,uint32_t,float,float,float,float);
}
void checked(int result){if(!result)throw std::runtime_error(slp_error());}
}
namespace starlight::editor {
AdventureController::AdventureController(const SceneData& scene,Vec3 eye){
    world_=slp_create();if(!world_)throw std::runtime_error(slp_error());
    try {
        auto terrain=scene.Terrain.value_or(TerrainData{});
        std::vector<float> heights(terrain.Heights.begin(),terrain.Heights.end());checked(slp_terrain(world_,heights.data()));
        for(const auto& object:scene.Characters){
            auto kind=object.Shape.value_or("");
            if(object.Dynamic || object.GameplayRole=="key" || object.GameplayRole=="exit" || kind=="light" || kind=="fire" || kind=="shadowenemy-smoke" || kind=="grass" || kind=="lava" || kind=="chaos-lightning")continue;
            auto mesh=object_mesh(object);std::vector<float> points;points.reserve(mesh.vertices.size()*3);
            for(const auto& v:mesh.vertices){auto p=world_point(object,v.position)-Vec3{object.X,object.Y,object.Z};points.insert(points.end(),{float(p.x),float(p.y),float(p.z)});}
            std::vector<uint32_t> indices(mesh.indices.begin(),mesh.indices.end());
            bool moving=(!object.Route.empty() || object.CombatPursuit || object.CombatHealth>0) && !object.Dynamic;
            auto id=slp_add(world_,points.data(),int(mesh.vertices.size()),indices.data(),int(indices.size()),moving?1:0,1000,float(object.X),float(object.Y),float(object.Z),0,0,0,1);
            if(id==UINT32_MAX)throw std::runtime_error(slp_error());
            bodies_.emplace(object.Id,id);baseAngles_.emplace(object.Id,std::pair{object.RotationY,object.RotationZ});
            if(moving){if(!object.Route.empty())platforms_.push_back(object);slp_character_platform(world_,id,float(object.X),float(object.Y),float(object.Z),1.f/60);}
        }
        checked(slp_character_start(world_,float(scene.Game->PlayerRadius),float(eye.x),float(eye.y-1.7),float(eye.z)));
    }catch(...){slp_destroy(world_);world_=nullptr;throw;}
}
void AdventureController::move_actor(const std::string& id,Vec3 position,double dt,double yawDegrees){
    if(auto it=bodies_.find(id);it!=bodies_.end()){auto angles=baseAngles_.at(id);slp_character_actor(world_,it->second,float(position.x),float(position.y),float(position.z),float((yawDegrees-angles.first)*.0174532925199433),float(angles.second*.0174532925199433),float(std::max(dt,1./120.)));}
}
AdventureController::~AdventureController(){if(world_)slp_destroy(world_);}
bool AdventureController::height(double value){auto result=slp_character_height(world_,float(value));if(result<0)throw std::runtime_error(slp_error());return result!=0;}
bool AdventureController::traverse(Vec3 eye,Vec3 travel){auto result=slp_character_traverse(world_,float(eye.x),float(eye.y-1.7),float(eye.z),float(travel.x),float(travel.y),float(travel.z));if(result<0)throw std::runtime_error(slp_error());return result!=0;}
Vec3 AdventureController::step(Vec3 eye,Vec3 velocity,double seconds,bool jump,double time,const std::unordered_set<std::string>& removed,bool overrideMotion){
    for(const auto& id:removed)if(auto it=bodies_.find(id);it!=bodies_.end()){slp_character_remove(world_,it->second);bodies_.erase(it);}
    const int count=std::max(1,int(std::ceil(seconds/(1./120))));const double dt=seconds/count;
    for(int i=0;i<count;i++){
        for(const auto& platform:platforms_)if(time>=0)if(auto body=bodies_.find(platform.Id);body!=bodies_.end()){
            auto point=platform.at(time+dt*(i+1));slp_character_platform(world_,body->second,float(point.X),float(platform.Y),float(point.Z),float(dt));
        }
        float output[4]{};
        if(overrideMotion)checked(slp_character_motion(world_,float(dt),float(eye.x),float(eye.y-1.7),float(eye.z),float(velocity.x),float(velocity.y),float(velocity.z),output));
        else checked(slp_character_step(world_,float(dt),float(eye.x),float(eye.y-1.7),float(eye.z),float(velocity.x),float(velocity.z),jump && i==0,output));
        eye={output[0],double(output[1])+1.7,output[2]};grounded=output[3]!=0;
    }
    return eye;
}
}
