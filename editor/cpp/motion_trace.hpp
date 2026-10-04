#pragma once
// Diagnostic only (event log): how many combat actors and physics bodies change position from one sample to the next, so the game side
// and the render side can be compared. Logs one summary every 120 samples; costs one pass over the scene while the event log is on.
#include "event_log.h"
#include "scene.hpp"
#include <array>
#include <string>
#include <unordered_map>
namespace starlight::editor {
struct MotionTrace {
    std::unordered_map<std::string,std::array<double,3>> last;
    uint64_t samples=0,actorMoves=0,bodyMoves=0,actors=0,bodies=0;
    void sample(const SceneData& scene,const char* tag){
        if(!starlight_diag::events_enabled())return;
        for(const auto& c:scene.Characters){
            const bool actor=c.CombatHealth>0,body=c.Dynamic&&!actor;
            if(!actor&&!body)continue;
            auto [it,inserted]=last.try_emplace(c.Id,std::array<double,3>{c.X,c.Y,c.Z});
            const bool moved=!inserted&&(it->second[0]!=c.X||it->second[1]!=c.Y||it->second[2]!=c.Z);
            it->second={c.X,c.Y,c.Z};
            if(actor){++actors;if(moved)++actorMoves;}else{++bodies;if(moved)++bodyMoves;}
        }
        if(++samples%120==0){
            starlight_diag::evlog(tag,double(actorMoves),double(actors),double(bodyMoves));
            actorMoves=actors=bodyMoves=bodies=0;
        }
    }
};
}
