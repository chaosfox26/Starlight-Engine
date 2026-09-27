#pragma once
#include "mesh.hpp"
#include <cstdint>
namespace starlight::editor {
struct LightningSoundEvent {uint64_t serial=0;Vec3 position{};double time=0;};
}
