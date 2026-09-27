#include "physics.hpp"
#include "body_constraints.hpp"
#include <DirectXMath.h>
#include <numbers>
namespace {
struct Liquid {
    uint32_t id;
    float height, ratio, padding;
};
struct Pose {
    uint32_t id;
    float x, y, z, qx, qy, qz, qw;
};
extern "C" {
__declspec(dllimport) int slp_body_current(void*,uint32_t,float,float);
__declspec(dllimport) int slp_kinematic_pose(void*,uint32_t,float,float,float,float,float,float,float,float);
__declspec(dllimport) void slp_body_velocity(void*,uint32_t,float,float,float,float,float,float);
__declspec(dllimport) int slp_body_material(void*,uint32_t,float,float);
__declspec(dllimport) void *slp_create();
__declspec(dllimport) void slp_destroy(void *);
__declspec(dllimport) void slp_clear(void *);
__declspec(dllimport) const char *slp_error();
__declspec(dllimport) int slp_terrain(void *, const float *);
__declspec(dllimport) int slp_ready(void *);
__declspec(dllimport) uint32_t slp_add(void *, const float *, int, const uint32_t *, int, int, float, float,
                                       float, float, float, float, float, float);
__declspec(dllimport) int slp_step(void *, double, const Liquid *, int, Pose *, int);
__declspec(dllimport) uint64_t slp_ticks(void *);
__declspec(dllimport) int slp_present(void *, Pose *, int);
}
void fail() { throw std::runtime_error(slp_error()); }
} // namespace
namespace starlight::editor {
PhysicsSession::PhysicsSession() : handle_(slp_create()) {
    if (!handle_)
        fail();
}
PhysicsSession::~PhysicsSession() { slp_destroy(handle_); }
void PhysicsSession::terrain(const TerrainData &t) {
    t.validate();
    std::vector<float> heights;
    heights.reserve(t.Heights.size());
    for (double height : t.Heights)
        heights.push_back(static_cast<float>(height));
    if (!slp_terrain(handle_, heights.data()))
        fail();
}
bool PhysicsSession::ready() {
    int result = slp_ready(handle_);
    if (result < 0)
        fail();
    return result != 0;
}
uint64_t PhysicsSession::ticks() const { return slp_ticks(handle_); }
void PhysicsSession::reset(const SceneData &scene) {
    scene.validate();
    slp_clear(handle_);
    bodies_.clear();kinematics_.clear();seconds_=0;
    std::unordered_map<std::string,uint32_t> jointBodies;
    terrain(scene.Terrain.value_or(TerrainData{}));
    for (const auto &c : scene.Characters) {
        auto k = c.Shape.value_or("");
        if (k == "grass" || k == "fire" || k == "light" || k == "lava" ||
            k == "chaos-lightning" || k == "light-shard" || k == "craxis-smoke")
            continue;
        auto mesh = object_mesh(c);
        std::vector<float> points;
        std::vector<uint32_t> indices(mesh.indices.begin(), mesh.indices.end());
        for (const auto &v : mesh.vertices) {
            points.push_back(float(v.position.x * c.Width * c.Size));
            points.push_back(float(v.position.y * c.Height * c.Size));
            points.push_back(float(v.position.z * c.Depth * c.Size));
        }
        using namespace DirectX;
        auto x = XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0),
                                          float(c.RotationX * std::numbers::pi / 180)),
             y = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0),
                                          float(c.RotationY * std::numbers::pi / 180)),
             z = XMQuaternionRotationAxis(XMVectorSet(0, 0, 1, 0),
                                          float(c.RotationZ * std::numbers::pi / 180));
        XMFLOAT4 q;
        XMStoreFloat4(&q, XMQuaternionMultiply(XMQuaternionMultiply(x, y), z));
        auto id =
            slp_add(handle_, points.data(), int(mesh.vertices.size()), indices.data(), int(indices.size()),
                    c.Dynamic||!c.Route.empty(), float(c.Density), float(c.X), float(c.Y), float(c.Z), q.x, q.y, q.z, q.w);
        if (id == UINT32_MAX)
            fail();
        if(!slp_body_material(handle_,id,float(c.Friction),float(c.Restitution)))fail();
        if(c.Dynamic)slp_body_velocity(handle_,id,float(c.VelocityX),float(c.VelocityY),float(c.VelocityZ),float(c.AngularVelocityX*.0174532925199433),float(c.AngularVelocityY*.0174532925199433),float(c.AngularVelocityZ*.0174532925199433));
        jointBodies[c.Id]=id;
        if(c.Dynamic||!c.Route.empty())bodies_[id]=c.Id;
        if(!c.Dynamic&&!c.Route.empty()){kinematics_[id]=c;if(!slp_kinematic_pose(handle_,id,float(c.X),float(c.Y),float(c.Z),q.x,q.y,q.z,q.w,1.f/60))fail();}
    }
    create_body_constraints(handle_,scene,jointBodies);
}
bool PhysicsSession::step(SceneData &scene, double dt, const TerrainFlow *flow) {
    if (!ready())
        return false;
    seconds_+=dt;
    for(const auto& [id,c]:kinematics_){using namespace DirectX;auto at=c.at(seconds_);float rad=float(std::numbers::pi/180);auto rotation=XMQuaternionMultiply(XMQuaternionMultiply(XMQuaternionRotationAxis(XMVectorSet(1,0,0,0),float(c.RotationX)*rad),XMQuaternionRotationAxis(XMVectorSet(0,1,0,0),float(c.RotationY)*rad)),XMQuaternionRotationAxis(XMVectorSet(0,0,1,0),float(c.RotationZ)*rad));rotation=XMQuaternionMultiply(rotation,XMQuaternionRotationAxis(XMVectorSet(0,1,0,0),float(c.route_yaw(seconds_))));XMFLOAT4 q;XMStoreFloat4(&q,rotation);if(!slp_kinematic_pose(handle_,id,float(at.X),float(c.Y),float(at.Z),q.x,q.y,q.z,q.w,float(std::max(dt,1./60.))))fail();}
    std::vector<Liquid> liquid;
    std::unordered_map<std::string, Character *> actors;
    for (auto &c : scene.Characters)
        actors[c.Id] = &c;
    for (const auto &[id, name] : bodies_) {
        auto it = actors.find(name);
        if (it == actors.end())
            throw std::runtime_error("Physics scene changed without reset");
        if (flow&&!kinematics_.contains(id)) {
            auto [height, density] = flow->liquid_at(it->second->X, it->second->Z);
            auto current=flow->velocity_at(it->second->X,it->second->Z);if(!slp_body_current(handle_,id,float(current.x),float(current.z)))fail();
            if (density > 0)
                liquid.push_back({id, float(height), float(density / it->second->Density), 0});
        }
    }
    std::vector<Pose> poses(bodies_.size());
    int count = slp_step(handle_, dt, liquid.data(), int(liquid.size()), poses.data(), int(poses.size()));
    if (count < 0)
        fail();
    count=slp_present(handle_,poses.data(),int(poses.size()));
    if(count<0)fail();
    for (int i = 0; i < count; i++) {
        const auto &p = poses[i];
        if(kinematics_.contains(p.id))continue;
        auto &c = *actors.at(bodies_.at(p.id));
        c.X = p.x;
        c.Y = p.y;
        c.Z = p.z;
        c.PhysicsRotation = std::array<double, 4>{p.qx, p.qy, p.qz, p.qw};
        update_rope_path(handle_,p.id,c,true);
    }
    return count > 0;
}
} // namespace starlight::editor
