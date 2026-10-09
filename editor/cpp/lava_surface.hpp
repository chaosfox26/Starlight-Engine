#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace starlight::editor {
struct LavaCrustSample {double cover=0,relief=0;};
inline double lava_cell_hash(int x,int z,uint32_t seed){
    uint32_t v=uint32_t(x)*0x9e3779b9u^uint32_t(z)*0x85ebca6bu^seed;v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;return double(v)/4294967296.;
}
inline double lava_smooth(double low,double high,double value){double u=std::clamp((value-low)/(high-low),0.,1.);return u*u*(3-2*u);}
// Identical integer seeds and cubic cell interpolation to slLavaFlowNoise.
// These broad fields drive actual relief and shader exposure together.
inline double lava_flow_noise(double x,double z,double frequency,uint32_t seed){
    x*=frequency;z*=frequency;int ix=int(std::floor(x)),iz=int(std::floor(z));double fx=x-ix,fz=z-iz,ux=fx*fx*(3-2*fx),uz=fz*fz*(3-2*fz);
    double a=lava_cell_hash(ix,iz,seed),b=lava_cell_hash(ix+1,iz,seed),c=lava_cell_hash(ix,iz+1,seed),d=lava_cell_hash(ix+1,iz+1,seed);
    double row0=a+(b-a)*ux,row1=c+(d-c)*ux;return row0+(row1-row0)*uz;
}
struct LavaPassageSample {double passage=0,exposed=0;};
inline LavaPassageSample lava_passage(double x,double z,double time,double heat){
    heat=std::clamp(heat,0.,1.);if(heat==0)time=0;
    double fx=x+time*.018,fz=z-time*.025;
    double islands=lava_flow_noise(fx,fz,1.6,83),folds=lava_flow_noise(fx*.7+time*.008,fz*1.6,4,59);
    double streams=lava_flow_noise(fx*.38+time*.009,fz*.73,2,97);
    // CPU evaluates the unfiltered material field; the shader widens/fades it
    // only for pixel footprints, as with the existing narrow cell cracks.
    double passage=1-lava_smooth(.017,.088+.08*islands,std::abs(streams-.48));
    double breakup=lava_smooth(.57,.78,islands+.14*folds),heatOpen=lava_smooth(.18,.80,heat);
    return {passage,std::clamp(heatOpen*(1-(1-passage)*(1-breakup*.82)),0.,1.)};
}
// The clock is written to a float vertex colour, so its steps coarsen with session age (about 8 ms at 36 hours).
// STARLIGHT_LAVA_TIME_PERIOD wraps it; the CPU crust and the shader share the wrapped clock, so the jump happens once per period.
inline double lava_time_clock(double time,double period){return period>0?std::fmod(time,period):time;}
inline LavaCrustSample lava_crust(double x,double z,double time,double heat,double depth,double thickness){
    // Stable plate cells drift coherently; their coverage drives both real
    // surface relief and shader cracks, rather than two unrelated patterns.
    heat=std::clamp(heat,0.,1.);
    if(heat==0)time=0;
    double u=(x+time*.018)*1.15+.22*std::sin(z*.7+time*.09)+.10*std::sin(z*2.3+x*.9),v=(z-time*.025)*1.15+.22*std::sin(x*.6-time*.07)+.10*std::sin(x*2.1-z*.8);
    int ix=int(std::floor(u)),iz=int(std::floor(v));u-=ix;v-=iz;double first=100,second=100,plate=0;
    for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){int xx=ix+dx,zz=iz+dz;double px=dx+.05+.9*lava_cell_hash(xx,zz,17),pz=dz+.05+.9*lava_cell_hash(xx,zz,29),d=std::hypot(px-u,pz-v);if(d<first){second=first;first=d;plate=lava_cell_hash(xx,zz,43);}else second=std::min(second,d);}
    double low=.012+.012*heat,high=.045+.075*heat,t=std::clamp((second-first-low)/(high-low),0.,1.);double cover=t*t*(3-2*t);
    double fx=x+time*.018,fz=z-time*.025,mass=lava_flow_noise(fx,fz,.65,131);
    // Weld neighbouring small cells into irregular larger basalt masses;
    // shared coarse fractures leave genuine recesses even in frozen crust.
    double weld=.8*lava_smooth(.22,.72,mass),fracture=1-lava_smooth(.008,.050,std::abs(mass-.49));
    cover=std::clamp((cover+(1-cover)*weld)*(1-fracture*.9)*(1-lava_passage(x,z,time,heat).exposed),0.,1.);
    double rib=lava_flow_noise(fx,fz,5.2,137),heightShape=.88+.08*mass+.04*rib;
    // The same coverage/exposure leaves the passages low, but surviving slabs
    // use more of the authored height and reach their plateau sooner. This
    // geometric bank profile never exceeds thickness or local depth bounds.
    double heightCover=cover/(.18+.82*cover);
    return {cover,heightCover*std::min(std::max(0.,thickness),std::max(0.,depth)*.3)*(.90+.10*(1-heat))*(.94+.06*plate)*heightShape};
}
}
