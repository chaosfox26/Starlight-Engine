#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Periodic material structure in a half-metre tile. These are surface maps,
// not replacement geometry: actual blades, fracture shapes and silhouettes
// remain the responsibility of their mesh builders.
namespace starlight::surface {
inline float hash(int x,int y,int seed){uint32_t v=uint32_t(x)*374761393u+uint32_t(y)*668265263u+uint32_t(seed)*2246822519u;v=(v^(v>>13))*1274126177u;return float(v^(v>>16))/4294967295.f;}
inline float noise(float u,float v,int period,int seed){
 float x=u*period,y=v*period;int ix=int(std::floor(x)),iy=int(std::floor(y));float a=x-ix,b=y-iy;a=a*a*(3-2*a);b=b*b*(3-2*b);
 auto h=[&](int px,int py){return hash((px%period+period)%period,(py%period+period)%period,seed);};
 return (h(ix,iy)*(1-a)+h(ix+1,iy)*a)*(1-b)+(h(ix,iy+1)*(1-a)+h(ix+1,iy+1)*a)*b;
}
struct Structure { std::array<float,3> color;float height,ao,roughness; };
struct Cell {float distance,gap,variation;};
inline Cell cells(float u,float v,int period,int seed){
 float px=u*period,py=v*period;int ix=int(std::floor(px)),iy=int(std::floor(py));
 float first=100,second=100,variation=0;
 for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
  int cx=ix+x,cy=iy+y;int wx=(cx%period+period)%period,wy=(cy%period+period)%period;
  float a=hash(wx,wy,seed),b=hash(wx,wy,seed+13);
  float dx=cx+.2f+.6f*a-px,dy=cy+.2f+.6f*b-py,d=std::sqrt(dx*dx+dy*dy);
  if(d<first){second=first;first=d;variation=hash(wx,wy,seed+29);}else second=std::min(second,d);
 }
 return {first,second-first,variation};
}
inline float smooth(float low,float high,float value){float t=std::clamp((value-low)/(high-low),0.f,1.f);return t*t*(3-2*t);}
inline float resolved(float frequency,float texel){return 1-smooth(.35f,1.2f,frequency*texel);}
struct Fibre {float cover=0,spine=0,variation=0;};
inline Fibre fibres(float u,float v,int period,int seed,float texel){
 float px=u*period,py=v*period;int ix=int(std::floor(px)),iy=int(std::floor(py));Fibre result;
 // Short curved botanical fragments, not a directional lighting pattern.
 // Neighbour motifs cross tile edges through wrapped seeds and unwrapped positions.
 for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
  int cx=ix+x,cy=iy+y,wx=(cx%period+period)%period,wy=(cy%period+period)%period;
  float angle=hash(wx,wy,seed)*6.2831853f,dx=px-(cx+hash(wx,wy,seed+7)),dy=py-(cy+hash(wx,wy,seed+13));
  float along=dx*std::cos(angle)+dy*std::sin(angle),across=-dx*std::sin(angle)+dy*std::cos(angle);
  float halfLength=.19f+.19f*hash(wx,wy,seed+19),t=along/halfLength;
  float bend=(hash(wx,wy,seed+23)-.5f)*.22f*(1-t*t);
  float width=(.026f+.020f*hash(wx,wy,seed+31))*std::sqrt(std::max(0.f,1-t*t));
  float footprint=std::max(.004f,period*texel*.65f),distance=std::abs(across-bend);
  float cover=(1-smooth(width-footprint,width+footprint,distance))*(1-smooth(halfLength-footprint,halfLength+footprint,std::abs(along)));
  cover*=resolved(period*14.f,texel);
  if(cover>result.cover)result={cover,cover*(1-smooth(.002f,footprint+.012f,distance)),hash(wx,wy,seed+37)};
 }
 return result;
}
inline Structure structure(float u,float v,int material,float texel){
 float broad=noise(u,v,4,17+material),grain=noise(u,v,32,31+material);
 float detail=resolved(128,texel),fine=.5f+(noise(u,v,128,43+material)-.5f)*detail;
 Structure m{{1,1,1},0,1,.8f};
 if(material==0){
  // Broken turf, fibrous thatch and irregular soil gaps under real grass.
  float patch=smooth(.32f,.72f,noise(u,v,8,57));auto leaf=fibres(u,v,12,81,texel);
  auto fragments=cells(u+.025f*(noise(u,v,8,83)-.5f),v+.025f*(noise(v,u,8,89)-.5f),52,95);
  float crumbs=(1-smooth(.06f,.28f+52*texel,fragments.distance))*resolved(104,texel);
  float grit=crumbs*(fragments.variation-.5f),litter=smooth(.59f,.82f,leaf.variation),organic=leaf.cover*(.4f+.6f*patch);
  // Broad turf colour stays restrained; small raised organic crumbs carry
  // the detail instead of large dark cloudy patches or a tiled contour net.
  float value=.32f+.075f*patch+.07f*grain+.09f*grit,dry=(1-patch)*.24f;
  m.color={value*(.70f+.15f*dry+.30f*litter)+organic*(.025f+.06f*litter),value*(1-.10f*dry)+organic*.048f,value*(.38f+.10f*dry+.22f*litter)+organic*.009f};
  m.height=.0012f*patch+.00028f*grain+.00042f*organic+.00009f*leaf.spine+.000085f*crumbs;
  m.ao=.84f+.085f*patch-.055f*leaf.cover*(1-leaf.spine)-.012f*crumbs;m.roughness=.85f+.07f*litter-.05f*leaf.spine+.03f*crumbs;
 }else if(material==1){
  // Rounded aggregates sit in fine earth rather than sharing rock veins.
  float wu=u+.04f*(noise(u,v,8,101)-.5f),wv=v+.04f*(noise(v,u,8,103)-.5f);
  auto c=cells(wu,wv,18,107);float aggregate=1-smooth(.10f,.42f,c.distance);
  aggregate=std::max(0.f,aggregate-texel*18*.35f);
  float pebble=aggregate*smooth(.35f,.85f,c.variation);
  // Drying creases follow a smooth broken field, not every aggregate's
  // cellular boundary; rounded crumbs must not become a cobblestone net.
  float drying=noise(wu,wv,7,113)-.5f+.18f*std::sin(6.2831853f*(wu*3+wv*2));
  float crack=(1-smooth(.006f,.019f+texel*7,std::abs(drying)))*smooth(.55f,.79f,noise(wu,wv,5,137))*resolved(64,texel);
  auto root=fibres(u,v,9,127,texel);float dryRoot=root.cover*smooth(.48f,.86f,root.variation);
  auto gritCell=cells(wu,wv,64,131);float grit=(1-smooth(.07f,.32f+64*texel,gritCell.distance))*resolved(128,texel);
  float mineral=grit*(gritCell.variation-.5f),value=.29f+.085f*grain+.095f*pebble-.018f*crack+.052f*dryRoot+.075f*mineral;
  m.color={value,value*(.75f+.10f*noise(u,v,24,117)),value*(.54f+.025f*grit)};
  m.height=.00065f*grain+.0021f*pebble-.00014f*crack+.00018f*dryRoot+.00011f*grit;
  m.ao=.89f-.11f*crack-.035f*aggregate+.05f*pebble-.015f*grit;m.roughness=.89f-.13f*pebble+.035f*dryRoot+.025f*grit;
 }else if(material==2){
  // Wind ripples warp continuously and repeat seamlessly across the tile.
  float phase=6.2831853f*(v*12+noise(u,v,4,149)*.8f+.20f*std::sin(u*6.2831853f*2));
  float ripple=std::pow(.5f+.5f*std::sin(phase),2),visibility=resolved(24,texel);
  ripple=.5f+(ripple-.5f)*visibility;
  auto grains=cells(u,v,52,151);float granule=(1-smooth(.07f,.32f+texel*52,grains.distance))*resolved(104,texel);
  float shell=granule*smooth(.83f,.96f,grains.variation),value=.55f+.09f*broad+.045f*grain+.035f*ripple+.08f*shell;
  m.color={value,value*.89f,value*.67f};m.height=.0014f*ripple+.00011f*granule+.00003f*fine;
  m.ao=.96f-.035f*(1-ripple)-.022f*granule;m.roughness=.84f+.07f*fine-.05f*shell;
 }else if(material==3){
  // Irregular granular minerals and scattered vesicles, without periodic
  // laminae or a connected cellular seam net. Small solid cores fade before
  // domain borders so individual grains never outline every mineral cell.
  float wu=u+.065f*(noise(u,v,4,163)-.5f),wv=v+.065f*(noise(v,u,4,173)-.5f);
  auto c=cells(wu,wv,37,191);
  float core=(1-smooth(.09f,.55f+texel*37,c.distance))*resolved(74,texel);
  float pale=core*smooth(.60f,.90f,c.variation),dark=core*smooth(.70f,.94f,1-c.variation);
  auto pits=cells(wu,wv,91,197);
  float pore=(1-smooth(.025f,.18f+texel*91,pits.distance))*resolved(182,texel);
  float granular=.5f+(grain-.5f)*resolved(64,texel);
  float mineral=.5f+(noise(wu,wv,12,193)-.5f)*resolved(24,texel),value=.24f+.085f*broad+.065f*granular+.02f*fine+.075f*pale-.035f*dark;
  m.color={value*(.96f+.04f*mineral),value*(.97f+.02f*mineral),value*(.98f-.025f*mineral)};
  m.height=.0010f*broad+.00065f*mineral+.00018f*core*(.3f+.7f*c.variation)+.00008f*fine-.00010f*pore;
  m.ao=.94f-.060f*pore-.035f*dark;m.roughness=.79f+.12f*granular+.05f*dark-.07f*pale;
 }else{
  // Soft snow drifts and granular ice. No glitter baked into albedo; the
  // existing filtered normals/roughness determine the actual response.
  float drift=noise(u,v,8,223),phase=6.2831853f*(v*5+noise(u,v,4,227)*.8f),ridge=.5f+.5f*std::sin(phase);
  auto snow=cells(u,v,44,229);float crystal=(1-smooth(.10f,.35f+texel*44,snow.distance))*resolved(88,texel);
  float value=.85f+.09f*drift+.025f*fine;
  m.color={value*.96f,value*.985f,value};m.height=.0013f*drift+.00065f*ridge+.00008f*crystal;
  m.ao=.96f+.025f*drift-.025f*crystal;m.roughness=.79f+.10f*grain-.09f*crystal;
 }
 return m;
}
}
