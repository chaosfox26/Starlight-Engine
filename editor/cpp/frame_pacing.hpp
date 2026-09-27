#pragma once
#include "platform.hpp"
#include "mesh.hpp"
#include <deque>
#include <map>
#include "../../deep/engine/parallel_metrics.h"
namespace starlight::editor {
struct MotionReading {
    std::string id,name;
    Vec3 position{};
    double updatesPerSecond=0, unchangedMs=0;
    bool moving=false;
};
struct PacingSnapshot {
    std::vector<double> frameMs;
    std::vector<MotionReading> objects;
    double fps=0,p50=0,p95=0,p99=0,worst=0,low1=0,low01=0;
    ParallelWorkReading multicore,battleWorkers;
    bool logFailed=false;
    uint64_t droppedLogRecords=0;
};
class FramePacingMonitor {
    struct Motion {
        std::vector<double> pose;
        std::deque<double> changes;
        std::string name;
        Vec3 position{};
        double first=0,lastSeen=0,lastChange=0;
        bool initialized=false;
    };
    std::map<std::string,Motion> motion_;
    std::deque<std::pair<double,double>> frames_;
    double previous_=-1;
public:
    void frame(double now) {
        if(previous_>=0)frames_.push_back({now,(now-previous_)*1000});
        previous_=now;
        while(!frames_.empty() && frames_.front().first<now-2)frames_.pop_front();
        for(auto it=motion_.begin();it!=motion_.end();) {
            if(it->second.lastSeen<now-.5)it=motion_.erase(it);else ++it;
        }
    }
    void pose(const std::string& id,const std::string& name,Vec3 position,std::vector<double> value,double now) {
        auto& m=motion_[id];m.name=name;m.position=position;m.lastSeen=now;
        if(!m.initialized){m.first=m.lastChange=now;m.initialized=true;}
        else if(m.pose!=value){m.changes.push_back(now);m.lastChange=now;}
        m.pose=std::move(value);
        while(!m.changes.empty() && m.changes.front()<now-2)m.changes.pop_front();
    }
    PacingSnapshot snapshot(double now) const {
        PacingSnapshot out;double total=0;
        for(auto f:frames_){out.frameMs.push_back(f.second);total+=f.second;}
        if(total>0)out.fps=1000*out.frameMs.size()/total;
        auto sorted=out.frameMs;std::sort(sorted.begin(),sorted.end());
        if(!sorted.empty()){
            auto q=[&](double p){return sorted[std::min(sorted.size()-1,size_t(std::ceil(p*sorted.size()))-1)];};
            out.p50=q(.5);out.p95=q(.95);out.p99=q(.99);out.worst=sorted.back();
            auto low=[&](double fraction){size_t count=std::max(size_t(1),size_t(std::ceil(sorted.size()*fraction)));double sum=0;for(size_t i=sorted.size()-count;i<sorted.size();++i)sum+=sorted[i];return sum>0?1000*count/sum:0;};
            out.low1=low(.01);out.low01=low(.001);
        }
        for(const auto& [id,m]:motion_){
            double window=std::min(2.,std::max(0.,now-m.first));
            out.objects.push_back({id,m.name,m.position,window>0?m.changes.size()/window:0,(now-m.lastChange)*1000,!m.changes.empty()});
        }
        return out;
    }
};
}
