#pragma once
#include "render_scene.hpp"
#include <unordered_map>
namespace starlight::editor {
class PhysicsSession {
    void *handle_ = nullptr;
    std::unordered_map<uint32_t, std::string> bodies_;
    std::unordered_map<uint32_t,Character> kinematics_;
    double seconds_=0;

  public:
    PhysicsSession();
    ~PhysicsSession();
    PhysicsSession(const PhysicsSession &) = delete;
    PhysicsSession &operator=(const PhysicsSession &) = delete;
    void terrain(const TerrainData &);
    void reset(const SceneData &);
    bool ready();
    bool step(SceneData &, double, const TerrainFlow *);
    uint64_t ticks() const;
};
} // namespace starlight::editor
