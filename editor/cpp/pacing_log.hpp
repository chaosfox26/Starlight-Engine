#pragma once
#include "frame_pacing.hpp"
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
namespace starlight::editor {
struct PacingFrameRecord {
    uint64_t frame=0;
    double time=0,intervalMs=0,sceneMs=0,submitMs=0,gpuMs=0;
    double simulationMs=0,combatAiMs=0,physicsMs=0,presentationMs=0;
    double fenceWaitMs=0,pacingWaitMs=0,presentWaitMs=0,inputSampleAgeMs=-1,inputEventAgeMs=-1;
    uint64_t updateSequence=0,inputSequence=0;
    std::vector<std::string> events;
    ParallelWorkReading multicore,battleWorkers;
    uint64_t gpuEffectCount=0,gpuEffectBytes=0;
};
inline json multicore_json(const ParallelWorkReading& r){return {{"parallelBatches",r.parallelBatches},{"serialBatches",r.serialBatches},{"participants",r.participants},{"items",r.items},{"wallMs",r.wallMs},{"callerWaitMs",r.callerWaitMs},{"chunkMs",r.chunkMs}};}
class PacingLog {
    struct Record {PacingSnapshot overlay;std::vector<PacingFrameRecord> frames;double time=0;};
    fs::path path_;
    std::mutex mutex_;std::condition_variable wake_;std::deque<Record> queue_;
    bool closing_=false;
    std::atomic<uint64_t> dropped_{0};std::atomic<bool> failed_{false};
    std::jthread writer_;
    void run(){
        try{
            fs::create_directories(path_.parent_path());std::ofstream file(path_,std::ios::binary);
            if(!file)throw std::runtime_error("Cannot open frame pacing log");
            file<<json{{"type","header"},{"version",4},{"process",GetCurrentProcessId()},
                {"scope","CPU render completion intervals; not display scanout"},
                {"lowDefinition","1000 / mean of slowest ceil(N*fraction) frame times; overlay uses rolling two seconds"},
                {"timingNotes","Simulation includes combat/AI and physics; frame call includes pacing/present waits. GPU time is delayed hardware query. Input ages are receipt/sample-to-render-completion proxies, not input-to-photon latency. Repeated updateSequence reuses the same simulation measurement. Multicore measures instrumented renderer pool callbacks, not OS core utilization. Chunk zero is the submitting thread; chunk times are elapsed wall time, can overlap and include preemption. Zero batches means this frame did not use this pool."},
                {"frameColumns",{"frame","sessionSeconds","intervalMs","sceneBuildUploadMs","frameCallMs","gpuMs","simulationMs","combatAiMs","physicsMs","presentationMs","fenceWaitMs","pacingWaitMs","presentWaitMs","inputSampleAgeMs","inputEventAgeMs","updateSequence","inputSequence","events","multicore","battleWorkers","gpuEffectCount","gpuEffectBytes"}}}.dump()<<'\n';
            while(true){Record record;{
                std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return closing_||!queue_.empty();});
                if(queue_.empty()&&closing_)break;record=std::move(queue_.front());queue_.pop_front();
            }
                json objects=json::array(),frames=json::array();
                for(auto& o:record.overlay.objects)objects.push_back({{"id",o.id},{"name",o.name},{"position",{o.position.x,o.position.y,o.position.z}},{"updatesPerSecond",o.updatesPerSecond},{"unchangedMs",o.unchangedMs},{"moving",o.moving},{"effectUpdatesPerSecond",o.effectUpdatesPerSecond},{"effectMeasurement",o.effectMeasurement}});
                for(auto& f:record.frames)frames.push_back({f.frame,f.time,f.intervalMs,f.sceneMs,f.submitMs,f.gpuMs,f.simulationMs,f.combatAiMs,f.physicsMs,f.presentationMs,f.fenceWaitMs,f.pacingWaitMs,f.presentWaitMs,f.inputSampleAgeMs,f.inputEventAgeMs,f.updateSequence,f.inputSequence,f.events,multicore_json(f.multicore),multicore_json(f.battleWorkers),f.gpuEffectCount,f.gpuEffectBytes});
                const auto& s=record.overlay;
                file<<json{{"type","sample"},{"sessionSeconds",record.time},{"frames",frames},
                    {"overlay",{{"fps",s.fps},{"medianMs",s.p50},{"p95Ms",s.p95},{"p99Ms",s.p99},{"worstMs",s.worst},{"low1Fps",s.low1},{"low01Fps",s.low01},{"graphMs",s.frameMs},{"objects",objects},{"multicore",multicore_json(s.multicore)},{"battleWorkers",multicore_json(s.battleWorkers)},{"gpuEffectCount",s.gpuEffectCount},{"gpuEffectBytes",s.gpuEffectBytes}}},
                    {"droppedRecords",dropped_.load()}}.dump()<<'\n';file.flush();
                if(!file)throw std::runtime_error("Cannot write frame pacing log");
            }
            file<<json{{"type","end"},{"droppedRecords",dropped_.load()}}.dump()<<'\n';
        }catch(...){failed_=true;}
    }
public:
    explicit PacingLog(fs::path path):path_(std::move(path)),writer_([this]{run();}){}
    ~PacingLog(){{std::lock_guard lock(mutex_);closing_=true;}wake_.notify_one();if(writer_.joinable())writer_.join();}
    void push(PacingSnapshot snapshot,std::vector<PacingFrameRecord> frames,double time){
        if(failed_)return;
        {std::lock_guard lock(mutex_);if(queue_.size()>=64){++dropped_;return;}queue_.push_back({std::move(snapshot),std::move(frames),time});}wake_.notify_one();
    }
    bool failed()const{return failed_;}
    uint64_t dropped()const{return dropped_;}
};
}
