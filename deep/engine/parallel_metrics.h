#pragma once
#include <algorithm>
#include <chrono>
#include <vector>

// One submitting thread. Each callback owns a separate timing slot; results
// are read only after the dispatcher has joined all participating workers.
struct ParallelWorkReading {
    unsigned parallelBatches=0, serialBatches=0, participants=0;
    unsigned long long items=0;
    double wallMs=0, callerWaitMs=0;
    std::vector<double> chunkMs;
};
class ParallelMetrics {
public:
    bool enabled=false;
    ParallelWorkReading reading;
    void reset(){reading={};}
    template<class Dispatch, class Function>
    void run(int count,unsigned participants,Dispatch dispatch,Function function){
        if(!enabled || count<=0){dispatch(function);return;}
        using Clock=std::chrono::steady_clock;
        std::vector<double> chunks(participants,0);
        auto started=Clock::now(),callerFinished=started;
        dispatch([&](int lo,int hi,int index){
            auto begin=Clock::now();function(lo,hi,index);auto end=Clock::now();
            chunks.at(index)=std::chrono::duration<double,std::milli>(end-begin).count();
            if(index==0)callerFinished=end;
        });
        auto finished=Clock::now();
        if(participants>1)++reading.parallelBatches;else ++reading.serialBatches;
        reading.participants=std::max(reading.participants,participants);
        reading.items+=count;
        reading.wallMs+=std::chrono::duration<double,std::milli>(finished-started).count();
        if(participants>1)reading.callerWaitMs+=std::chrono::duration<double,std::milli>(finished-callerFinished).count();
        reading.chunkMs.resize(std::max(reading.chunkMs.size(),chunks.size()),0);
        for(size_t i=0;i<chunks.size();++i)reading.chunkMs[i]+=chunks[i];
    }
};
