#pragma once
// Runtime switches for the combined-scene repairs, so every repair can be measured on and off in the same executable.
// Each defaults to on; set the variable to 0 to restore the previous behaviour.
#include <windows.h>
namespace starlight::editor {
inline bool repair_switch(const char* name){char value[8]{};return !(GetEnvironmentVariableA(name,value,8)&&value[0]=='0');}
inline bool repair_bounds(){static const bool on=repair_switch("STARLIGHT_FIX_BOUNDS");return on;}           // cached object bounds + ray prefilter in gameplay queries
inline bool repair_delta(){static const bool on=repair_switch("STARLIGHT_FIX_DELTA");return on;}             // combat actors drawn in the delta; liquid obstacle mask reused
inline bool repair_cubebounds(){static const bool on=repair_switch("STARLIGHT_FIX_CUBEBOUNDS");return on;}   // plain cubes are posed from their eight corners in the shared bounds cache, with no mesh built or cached
inline bool repair_hullbounds(){static const bool on=repair_switch("STARLIGHT_FIX_HULLBOUNDS");return on;}   // moving pieces are measured from their convex-hull vertices, not every vertex
inline bool repair_turn(){static const bool on=repair_switch("STARLIGHT_FIX_TURN");return on;}                 // turning in place tolerates shallow contact with neighbours
inline bool repair_solids(){static const bool on=repair_switch("STARLIGHT_FIX_SOLIDS");return on;}               // enemies are not blocked by grass, lights, fire, keys (same rule the player controller uses)
inline bool repair_instances(){static const bool on=repair_switch("STARLIGHT_FIX_INSTANCES");return on;}      // moving physics bodies are drawn as resident instances (geometry once, pose per update)
inline bool repair_prefracture(){static const bool on=repair_switch("STARLIGHT_FIX_PREFRACTURE");return on;} // rolling break-ahead fracture
}
