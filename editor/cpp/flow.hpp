#pragma once
#include "geometry.hpp"
namespace starlight::editor {
struct FlowObstacleMask {
    // Keep dense masks off the 1 MiB Windows thread stack. Value copies still own their storage.
    std::vector<double> bottom = std::vector<double>(2304), top = std::vector<double>(2304),
                        coverage = std::vector<double>(2304);
    std::vector<double> low = std::vector<double>(9216, INFINITY),
                        high = std::vector<double>(9216, -INFINITY);
    void add(const Mesh &, const Character &);
    void finish();
};
class TerrainFlow {
    TerrainData terrain_;
    std::vector<LiquidSource> sources_;
    std::vector<double> remaining_;
    std::array<double, 2304> bed_{}, outflow_{}, delta_{};
    struct Transfer {
        int a, b;
        double amount;
    };
    std::vector<Transfer> transfers_;
    std::optional<FlowObstacleMask> obstacles_;
    void transport(std::array<double, 2304> &, double, double);

  public:
    static constexpr int N = 48;
    static constexpr double Cell = .5, Area = .25;
    std::array<double, 2304> water{}, lava{};
    double emitted = 0;
    TerrainFlow(const TerrainData &, const std::vector<LiquidSource> &);
    static int index(double x, double z) {
        return std::clamp(int((z + 12) / Cell), 0, N - 1) * N + std::clamp(int((x + 12) / Cell), 0, N - 1);
    }
    void set_obstacles(FlowObstacleMask mask) { obstacles_ = std::move(mask); }
    double surface(int, double) const;
    std::pair<double, double> liquid_at(double, double) const;
    double volume() const;
    void step(double);
    std::vector<MeshPart> geometry() const;
};
} // namespace starlight::editor
