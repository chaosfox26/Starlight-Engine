#pragma once
#include "adventure.hpp"
#include <windows.h>
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
// Bounded independent waveOut voices; audio never sleeps or blocks gameplay.
// Buffers remain alive until the device returns them, including on shutdown.
class GameplayAudio {
    struct Voice{HWAVEOUT device{};WAVEHDR header{};std::vector<short> pcm;};
    std::array<Voice,8> voices_{};
    uint64_t epoch_=0,shots_=0,shatters_=0,impacts_=0,lightning_=0;
    double chirpAt_=0;
    struct PendingThunder {LightningSoundEvent event;double arrival;};
    std::vector<PendingThunder> thunder_;
    bool baseline_=false;
    void release(Voice& v){if(!v.device)return;waveOutReset(v.device);waveOutUnprepareHeader(v.device,&v.header,sizeof(v.header));waveOutClose(v.device);v.device=nullptr;v.header={};v.pcm.clear();}
public:
    uint64_t submitted=0,failed=0;
    GameplayAudio()=default;
    GameplayAudio(const GameplayAudio&)=delete;
    GameplayAudio& operator=(const GameplayAudio&)=delete;
    ~GameplayAudio(){stop();}
    void stop(){for(auto& v:voices_)release(v);baseline_=false;thunder_.clear();}
    void play(GameplayCue cue,double gain=1,double pan=0){
        for(auto& v:voices_){
            if(v.device && !(v.header.dwFlags&WHDR_DONE))continue;
            release(v);v.pcm=stereo_sound(gameplay_sound(cue),gain,pan);
            WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=22050;format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=88200;
            if(waveOutOpen(&v.device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){v.device=nullptr;++failed;return;}
            v.header.lpData=reinterpret_cast<LPSTR>(v.pcm.data());v.header.dwBufferLength=static_cast<DWORD>(v.pcm.size()*sizeof(short));
            if(waveOutPrepareHeader(v.device,&v.header,sizeof(v.header))!=MMSYSERR_NOERROR || waveOutWrite(v.device,&v.header,sizeof(v.header))!=MMSYSERR_NOERROR){++failed;release(v);return;}
            ++submitted;return;
        }
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
