#pragma once
#include "scene.hpp"
#include <limits>
namespace starlight::editor {
struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return *this * (1 / s); }
    double dot(Vec3 b) const { return x * b.x + y * b.y + z * b.z; }
    Vec3 cross(Vec3 b) const { return {y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x}; }
    double length() const { return std::sqrt(dot(*this)); }
    Vec3 unit() const {
        double n = length();
        return n > 1e-15 ? *this / n : Vec3{};
    }
};
inline uint64_t vertex_motion_identity(uint64_t domain,uint64_t instance,uint64_t vertex){uint64_t hash=1469598103934665603ull;for(auto value:{domain,instance,vertex})for(int i=0;i<8;++i){hash^=(value>>(i*8))&255;hash*=1099511628211ull;}return hash?hash:1;}
struct Vertex {
    Vec3 position, normal;
    Vec3 color{1, 1, 1};
    float kind = 0;
    uint64_t motionIdentity=0;
};
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<int> indices;
    void validate() const;
    void normals();
};
Mesh mesh_from(const SculptMesh &);
SculptMesh sculpt_from(const Mesh &);
Character route_pose(const Character &,double seconds);
Vec3 world_point(const Character &, Vec3);
Vec3 local_point(const Character &, Vec3);
struct MeshHit {
    Vec3 point, normal;
    double distance = std::numeric_limits<double>::infinity();
};
std::optional<MeshHit> mesh_hit(const Mesh &, const Character &, Vec3 origin, Vec3 direction);
SculptMesh prepare_sculpt(const Mesh &);
void sculpt_brush(Character &, Vec3 hit, Vec3 normal, double radius, double strength, std::string_view tool,
                  bool reverse, double dt);
// One transaction is one undo step, including continuous brush strokes.
class SceneHistory {
    std::vector<SceneData> undo_, redo_;
    std::optional<SceneData> pending_;

  public:
    void begin(const SceneData &s) {
        if (pending_)
            throw std::logic_error("Edit already active");
        pending_ = s;
    }
    void commit() {
        if (!pending_)
            return;
        undo_.push_back(std::move(*pending_));
        pending_.reset();
        redo_.clear();
    }
    void cancel(SceneData &s) {
        if (pending_) {
            s = std::move(*pending_);
            pending_.reset();
        }
    }
    bool undo(SceneData &s) {
        if (pending_)
            cancel(s);
        if (undo_.empty())
            return false;
        redo_.push_back(std::move(s));
        s = std::move(undo_.back());
        undo_.pop_back();
        return true;
    }
    bool redo(SceneData &s) {
        if (pending_)
            throw std::logic_error("Edit active");
        if (redo_.empty())
            return false;
        undo_.push_back(std::move(s));
        s = std::move(redo_.back());
        redo_.pop_back();
        return true;
    }
    void clear() {
        undo_.clear();
        redo_.clear();
        pending_.reset();
    }
};
} // namespace starlight::editor
