#pragma once
#include "interactions.hpp"
#include <unordered_map>
extern "C" __declspec(dllimport) int slp_rope(void*,uint32_t,uint32_t,const float*,int,float);
extern "C" __declspec(dllimport) int slp_rope_path(void*,uint32_t,float*,int,int);
extern "C" __declspec(dllimport) int slp_joint(void*,uint32_t,uint32_t,int,const float*);
extern "C" __declspec(dllimport) const char* slp_error();
namespace starlight::editor {
inline void update_rope_path(void* world,uint32_t body,Character& c,bool interpolate){c.JointRuntimePoints.clear();if(c.JointKind!=3||!c.JointSegments)return;float path[65*3]{};int count=slp_rope_path(world,body,path,65,interpolate);if(count<0)throw std::runtime_error(slp_error());for(int i=0;i<count;++i)c.JointRuntimePoints.push_back({path[i*3],path[i*3+1],path[i*3+2]});}
inline void create_body_constraints(void* world,const SceneData& scene,const std::unordered_map<std::string,uint32_t>& bodies){
    for(const auto& c:scene.Characters)if(c.JointKind){
        auto source=bodies.find(c.Id);if(source==bodies.end())throw std::runtime_error("Joint source has no collision body: "+c.Name);
        uint32_t target=UINT32_MAX;
        auto a=world_point(c,{c.JointX,c.JointY,c.JointZ});
        Vec3 b{c.X+c.JointTargetX,c.Y+c.JointTargetY,c.Z+c.JointTargetZ};
        if(!c.JointTargetId.empty()){
            auto body=bodies.find(c.JointTargetId);if(body==bodies.end())throw std::runtime_error("Joint target has no collision body: "+c.Name);target=body->second;
            for(const auto& other:scene.Characters)if(other.Id==c.JointTargetId){b=world_point(other,{c.JointTargetX,c.JointTargetY,c.JointTargetZ});break;}
        }
        float settings[]={float(a.x),float(a.y),float(a.z),float(b.x),float(b.y),float(b.z),float(c.JointAxisX),float(c.JointAxisY),float(c.JointAxisZ),float(c.JointLength),float(c.JointFrequency),float(c.JointDamping),float(c.JointAngleMin*.0174532925199433),float(c.JointAngleMax*.0174532925199433)};
        int result=c.JointKind==3&&c.JointSegments?slp_rope(world,source->second,target,settings,c.JointSegments,float(c.JointRadius)):slp_joint(world,source->second,target,c.JointKind,settings);
        if(!result)throw std::runtime_error(slp_error());
    }
}
}
