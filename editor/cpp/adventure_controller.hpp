#pragma once
#include "render_scene.hpp"
#include <unordered_map>
#include <unordered_set>
namespace starlight::editor {
// One collision world per Play session. Moving routes are translational,
// convex kinematic platforms and dynamic props share the character collision world.
class AdventureController {
    void* world_=nullptr;
    std::unordered_map<std::string,uint32_t> bodies_;
    std::vector<Character> platforms_;
    std::unordered_map<std::string,std::pair<double,double>> baseAngles_;
    std::unordered_set<std::string> dynamic_;
    std::unordered_map<uint32_t,double> liquidBodies_;
    std::shared_ptr<const LiquidSnapshot> liquids_;
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
    void sync_dynamic(SceneData&);
    void impulse_body(uint32_t,Vec3,Vec3);
    void impulse(const std::string&,Vec3 position,Vec3 momentum);
    void liquids(std::shared_ptr<const LiquidSnapshot> value){liquids_=std::move(value);}
    std::pair<Vec3,Vec3> body_motion(const std::string&,Vec3)const;
    uint32_t add_debris(const Mesh&,Vec3,Vec3,double,double,double,std::optional<Vec3> spin={});
    void debris_pose(uint32_t,Vec3&,std::array<double,4>&);
    void remove_debris(uint32_t);

};
}
