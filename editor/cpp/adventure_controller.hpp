#pragma once
#include "render_scene.hpp"
#include <unordered_map>
#include <unordered_set>
namespace starlight::editor {
// One collision world per Play session. Moving routes are translational,
// convex kinematic platforms; dynamic rigid-body interaction is out of scope.
class AdventureController {
    void* world_=nullptr;
    std::unordered_map<std::string,uint32_t> bodies_;
    std::vector<Character> platforms_;
    std::unordered_map<std::string,std::pair<double,double>> baseAngles_;
public:
    AdventureController(const SceneData&,Vec3);
    ~AdventureController();
    AdventureController(const AdventureController&)=delete;
    AdventureController& operator=(const AdventureController&)=delete;
    Vec3 step(Vec3,Vec3,double,bool,double,const std::unordered_set<std::string>&,bool overrideMotion=false);
    bool grounded=false;
    bool height(double);
    bool traverse(Vec3 eye,Vec3 travel);
    void move_actor(const std::string&,Vec3,double,double yawDegrees=0);
};
}
