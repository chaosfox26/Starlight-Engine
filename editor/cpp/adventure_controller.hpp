#pragma once
#include "render_scene.hpp"
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
namespace starlight::editor {
// A convex collision hull for one debris fragment, built ahead of time and independent of any physics world.
// Released automatically when the last reference goes (on any thread).
using DebrisHull=std::shared_ptr<const void>;
DebrisHull build_debris_hull(const Mesh& mesh,double density); // safe on any thread; null if it cannot be built
// What the hull builder did for one hull: kind 0 exact, 1 exact after a finer-tolerance retry, 2 reduced to the library's 256-vertex limit (deviation is the
// measured farthest distance of an input point from the hull), 3 bounding box, 4 rebuilt from the builder's own hull vertices (deviation measured).
struct HullDetail {int kind=0,inputPoints=0,hullPoints=0;float deviation=0,diagonal=0;};
bool hull_detail(const DebrisHull&,HullDetail&);
std::vector<std::array<float,3>> hull_points(const DebrisHull&);
struct HullReport {uint64_t requested=0,exact=0,exactAfterFinerRetry=0,reducedToLimit=0,reducedBeyondBound=0,boxFallbacks=0,rejected=0,unbuildable=0,rebuiltFromHullVertices=0;double worstReducedRelativeDeviation=0;std::string lastBoxFallback,lastUnbuildable;};
HullReport hull_report();
void reset_hull_report();
struct ShapeCacheReport {size_t shapes=0,bytes=0;uint64_t hits=0,misses=0,trimmed=0;};
ShapeCacheReport shape_cache_report();
// Motionless objects are given to the physics world in their own space with their turn as the body rotation, so objects with the same geometry and scale share
// one cooked shape whatever way they are turned. Off restores baking each turn into the vertices.
void share_static_shapes(bool);
bool share_static_shapes();
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
    void terrain(const TerrainData&); // Replace ground collision without rebuilding bodies or the character.
    bool traverse(Vec3 eye,Vec3 travel);
    void move_actor(const std::string&,Vec3,double,double yawDegrees=0);
    void sync_dynamic(SceneData&);
    void impulse_body(uint32_t,Vec3,Vec3);
    void impulse(const std::string&,Vec3 position,Vec3 momentum);
    void liquids(std::shared_ptr<const LiquidSnapshot> value){liquids_=std::move(value);}
    std::pair<Vec3,Vec3> body_motion(const std::string&,Vec3)const;
    uint32_t add_debris(const Mesh&,Vec3,Vec3,double,double,double,std::optional<Vec3> spin={},const DebrisHull& hull={},std::array<double,4> rotation={0,0,0,1}); // a prebuilt hull (made for this exact mesh and density) skips hull construction
    void debris_pose(uint32_t,Vec3&,std::array<double,4>&);
    void remove_debris(uint32_t);

};
}
