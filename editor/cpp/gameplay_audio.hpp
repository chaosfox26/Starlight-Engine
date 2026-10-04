#pragma once
#include "adventure.hpp"
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <mmsystem.h>
#pragma comment(lib,"winmm.lib")
namespace starlight::editor {
enum class GameplayCue { Shot, Shatter, Impact, Chirp, Lightning };
// Original synthesized placeholder cues. No external assets or file access.
inline std::vector<short> gameplay_sound(GameplayCue cue){
    constexpr double rate=22050,pi=3.14159265358979323846;
    const double duration=cue==GameplayCue::Lightning?.65:cue==GameplayCue::Shatter?.24:cue==GameplayCue::Impact?.18:.10;
    std::vector<short> pcm(static_cast<size_t>(rate*duration));uint32_t noise=0x51a7;
    for(size_t i=0;i<pcm.size();++i){
        double t=i/rate,u=t/duration;noise=noise*1664525u+1013904223u;
        double n=double(noise>>8)/8388607.5-1,signal=0;
        switch(cue){
        case GameplayCue::Lightning:signal=.8*n+.2*std::sin(2*pi*120*t);break;
        case GameplayCue::Shot:signal=.65*n+.35*std::sin(2*pi*(320*t-1000*t*t));break;
        case GameplayCue::Shatter:signal=.55*n+.45*std::sin(2*pi*1800*t)*std::sin(2*pi*2730*t);break;
        case GameplayCue::Impact:signal=.25*n+.75*std::sin(2*pi*85*t);break;
        case GameplayCue::Chirp:signal=std::sin(2*pi*(1400*t+6000*t*t));break;
        }
        double envelope=std::min(1.,t/.003)*std::pow(1-u,2);
        pcm[i]=static_cast<short>(signal*envelope*6500);
    }
    return pcm;
}
struct SpatialSound {double delay=0,gain=1,pan=0;};
inline SpatialSound spatial_sound(Vec3 source,Vec3 listener,Vec3 forward,Vec3 up){auto delta=source-listener;double distance=delta.length();auto right=forward.cross(up).unit();return {distance/343.,1./(1.+distance*distance/100.),distance>1e-6?std::clamp(delta.dot(right)/distance,-1.,1.):0};}
inline std::vector<short> stereo_sound(std::span<const short> mono,double gain,double pan){gain=std::clamp(gain,0.,1.);pan=std::clamp(pan,-1.,1.);double left=gain*std::sqrt((1-pan)*.5),right=gain*std::sqrt((1+pan)*.5);std::vector<short> out;out.reserve(mono.size()*2);for(auto sample:mono){out.push_back(short(sample*left));out.push_back(short(sample*right));}return out;}
// Bounded independent waveOut voices. Opening and closing a wave device blocks for 8-30 ms in the driver, so every device call
// runs on a dedicated audio thread: play() and stop() only queue a request and never block gameplay (the first shatter used to
// freeze camera and character updates for about 30 ms). Devices stay open between sounds. Buffers remain alive until the device
// returns them, including on shutdown.
class GameplayAudio {
    struct Voice{HWAVEOUT device{};WAVEHDR header{};std::vector<short> pcm;bool prepared=false;};
    struct Request{bool stopAll=false;GameplayCue cue=GameplayCue::Shot;double gain=1,pan=0;};
    std::array<Voice,8> voices_{};
    std::array<std::vector<short>,5> cues_{};
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<Request> queue_;
    std::atomic<bool> voicesOpen_{false};
    uint64_t epoch_=0,shots_=0,shatters_=0,impacts_=0,lightning_=0;
    double chirpAt_=0;
    struct PendingThunder {LightningSoundEvent event;double arrival;};
    std::vector<PendingThunder> thunder_;
    bool baseline_=false;
    // Audio-thread only: the device stays open so the next sound does not pay for waveOutOpen.
    void retire(Voice& v){if(v.prepared){waveOutUnprepareHeader(v.device,&v.header,sizeof(v.header));v.prepared=false;}v.header={};v.pcm.clear();}
    void release(Voice& v){if(!v.device)return;waveOutReset(v.device);retire(v);waveOutClose(v.device);v.device=nullptr;}
    void play_now(const Request& r){
        if(cues_[size_t(r.cue)].empty())cues_[size_t(r.cue)]=gameplay_sound(r.cue);
        for(auto& v:voices_){
            if(v.device && !(v.header.dwFlags&WHDR_DONE))continue;
            if(v.device)retire(v);
            v.pcm=stereo_sound(cues_[size_t(r.cue)],r.gain,r.pan);
            if(!v.device){
                WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=22050;format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=88200;
                if(waveOutOpen(&v.device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){v.device=nullptr;v.pcm.clear();std::atomic_ref<uint64_t>(failed).fetch_add(1);return;}
            }
            v.header.lpData=reinterpret_cast<LPSTR>(v.pcm.data());v.header.dwBufferLength=static_cast<DWORD>(v.pcm.size()*sizeof(short));
            if(waveOutPrepareHeader(v.device,&v.header,sizeof(v.header))!=MMSYSERR_NOERROR){std::atomic_ref<uint64_t>(failed).fetch_add(1);release(v);return;}
            v.prepared=true;
            if(waveOutWrite(v.device,&v.header,sizeof(v.header))!=MMSYSERR_NOERROR){std::atomic_ref<uint64_t>(failed).fetch_add(1);release(v);return;}
            voicesOpen_=true;std::atomic_ref<uint64_t>(submitted).fetch_add(1);return;
        }
    }
    void run(std::stop_token stop){
        for(;;){
            Request r;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock,stop,[&]{return !queue_.empty();});
                if(queue_.empty())break; // stop requested
                r=queue_.front();queue_.pop_front();
            }
            if(r.stopAll){for(auto& v:voices_)release(v);voicesOpen_=false;}
            else play_now(r);
        }
        for(auto& v:voices_)release(v);
    }
    std::jthread worker_;
public:
    // Written by the audio thread through atomic_ref; other threads read them as plain integers (aligned 64-bit reads on x64).
    uint64_t submitted=0,failed=0;
    GameplayAudio():worker_([this](std::stop_token stop){run(stop);}){}
    GameplayAudio(const GameplayAudio&)=delete;
    GameplayAudio& operator=(const GameplayAudio&)=delete;
    ~GameplayAudio(){worker_.request_stop();wake_.notify_all();worker_.join();}
    void stop(){
        baseline_=false;thunder_.clear();
        if(!voicesOpen_.load()){std::lock_guard lock(mutex_);if(queue_.empty())return;}
        {std::lock_guard lock(mutex_);queue_.clear();queue_.push_back({true});}
        wake_.notify_one();
    }
    void play(GameplayCue cue,double gain=1,double pan=0){
        {std::lock_guard lock(mutex_);if(queue_.size()>=16)return;queue_.push_back({false,cue,gain,pan});}
        wake_.notify_one();
    }
    void update(const AdventurePlayer::Presentation* p,bool enabled){
        if(!p || !enabled || !p->audioActive){stop();return;}
        if(!baseline_ || p->epoch!=epoch_){
            stop();baseline_=true;epoch_=p->epoch;shots_=p->shots;shatters_=p->shatters;impacts_=p->impacts;lightning_=p->lightning;chirpAt_=p->poseTime;
        }
        if(p->shots!=shots_)play(GameplayCue::Shot);
        if(p->shatters!=shatters_)play(GameplayCue::Shatter);
        for(const auto& event:p->lightningSounds)if(event.serial>lightning_){auto sound=spatial_sound(event.position,p->eye,p->viewAim,p->viewUp);if(thunder_.size()<128)thunder_.push_back({event,event.time+sound.delay});}
        std::erase_if(thunder_,[&](const auto& pending){if(p->poseTime<pending.arrival)return false;auto sound=spatial_sound(pending.event.position,p->eye,p->viewAim,p->viewUp);play(GameplayCue::Lightning,sound.gain,sound.pan);return true;});
        if(p->impacts!=impacts_)play(GameplayCue::Impact);
        shots_=p->shots;shatters_=p->shatters;impacts_=p->impacts;lightning_=p->lightning;
        if(p->control==ControlEffect::Stun && p->poseTime>=chirpAt_){play(GameplayCue::Chirp);chirpAt_=p->poseTime+.4;}
        if(p->control!=ControlEffect::Stun)chirpAt_=p->poseTime;
    }
};
}
