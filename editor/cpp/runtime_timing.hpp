#pragma once
#include <chrono>
#include "../../deep/engine/parallel_metrics.h"
namespace starlight::editor {
inline double timing_seconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct RuntimeTiming {double simulationMs=0,combatAiMs=0,physicsMs=0,presentationMs=0,inputSampleTime=0;ParallelWorkReading battleWorkers;};
struct ScopedTiming {double& value;double start=timing_seconds();~ScopedTiming(){value+=(timing_seconds()-start)*1000;}};
}
