#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>
#include "surface_structure.h"

namespace starlight::surface {
struct Level {uint32_t size=0;std::array<std::vector<uint8_t>,3> maps;};
using Tile=std::vector<Level>;
inline float linear(float c){return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);}
inline float encoded(float c){c=std::clamp(c,0.f,1.f);return c<=.0031308f?c*12.92f:1.055f*std::pow(c,1/2.4f)-.055f;}
inline uint8_t byte(float c){return uint8_t(std::lround(std::clamp(c,0.f,1.f)*255));}
inline void appendMips(Tile& tile){
 if(tile.empty()||!tile[0].size||(tile[0].size&(tile[0].size-1)))throw std::runtime_error("Surface tile must be a power of two");
 for(const auto& map:tile[0].maps)if(map.size()!=size_t(tile[0].size)*tile[0].size*4)throw std::runtime_error("Incomplete surface map");
 while(tile.back().size>1){
  const auto& src=tile.back();Level dst;dst.size=src.size/2;for(auto& map:dst.maps)map.resize(size_t(dst.size)*dst.size*4);
  for(uint32_t y=0;y<dst.size;++y)for(uint32_t x=0;x<dst.size;++x){
   float albedo[4]{},normal[3]{},ao=0,rough2=0,metal=0;
   for(uint32_t dy=0;dy<2;++dy)for(uint32_t dx=0;dx<2;++dx){
    size_t k=(size_t(y*2+dy)*src.size+x*2+dx)*4;
    for(int c=0;c<3;++c)albedo[c]+=linear(src.maps[0][k+c]/255.f)*.25f;
    albedo[3]+=src.maps[0][k+3]/1020.f;
    float nx=src.maps[1][k]/127.5f-1,ny=src.maps[1][k+1]/127.5f-1,nz=src.maps[1][k+2]/127.5f-1;
    float length=std::sqrt(nx*nx+ny*ny+nz*nz);if(length<.01f)throw std::runtime_error("Invalid surface normal");
    normal[0]+=nx/length*.25f;normal[1]+=ny/length*.25f;normal[2]+=nz/length*.25f;
    ao+=src.maps[2][k]/1020.f;float r=src.maps[2][k+1]/255.f;rough2+=r*r*.25f;metal+=src.maps[2][k+2]/1020.f;
   }
   size_t k=(size_t(y)*dst.size+x)*4;float len=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
   for(int c=0;c<3;++c){dst.maps[0][k+c]=byte(encoded(albedo[c]));dst.maps[1][k+c]=byte((len>.001f?normal[c]/len:(c==2?1.f:0.f))*.5f+.5f);}
   dst.maps[0][k+3]=byte(albedo[3]);dst.maps[1][k+3]=255;
   dst.maps[2][k]=byte(ao);dst.maps[2][k+1]=byte(std::sqrt(std::min(1.f,rough2+std::max(0.f,1-len)*.5f)));dst.maps[2][k+2]=byte(metal);dst.maps[2][k+3]=255;
  }
  tile.push_back(std::move(dst));
 }
}
inline Tile builtin(uint32_t size,int material){
 if(size<16||size>2048||(size&(size-1))||material<0||material>=5)throw std::runtime_error("Invalid built-in surface texture");
 Tile tile(1);auto& level=tile[0];level.size=size;for(auto& map:level.maps)map.resize(size_t(size)*size*4);
 std::vector<float> heights(size_t(size)*size);
 for(uint32_t y=0;y<size;++y)for(uint32_t x=0;x<size;++x){
  float u=(x+.5f)/size,v=(y+.5f)/size;auto m=structure(u,v,material,1.f/size);
  heights[size_t(y)*size+x]=m.height;
  size_t k=(size_t(y)*size+x)*4;for(int c=0;c<3;++c)level.maps[0][k+c]=byte(encoded(m.color[c]));level.maps[0][k+3]=255;
  level.maps[2][k]=byte(m.ao);level.maps[2][k+1]=byte(m.roughness);level.maps[2][k+2]=0;level.maps[2][k+3]=255;
 }
 for(uint32_t y=0;y<size;++y)for(uint32_t x=0;x<size;++x){
  auto h=[&](uint32_t px,uint32_t py){return heights[size_t(py%size)*size+px%size];};
  float dx=(h(x+1,y)-h((x+size-1)%size,y))*size,dy=(h(x,y+1)-h(x,(y+size-1)%size))*size;
  float length=std::sqrt(dx*dx+dy*dy+1);size_t k=(size_t(y)*size+x)*4;level.maps[1][k]=byte(.5f-.5f*dx/length);level.maps[1][k+1]=byte(.5f-.5f*dy/length);level.maps[1][k+2]=byte(.5f+.5f/length);level.maps[1][k+3]=255;
 }
 appendMips(tile);return tile;
}
inline std::array<Tile,5> builtinSet(uint32_t size,unsigned workers=std::thread::hardware_concurrency()){
 // Materials are independent. Limit concurrent preparation to the five tiles
 // and keep all mip/normal/roughness calculations identical to builtin().
 workers=std::clamp(workers,1u,5u);std::array<Tile,5> tiles;
 if(workers==1){for(int i=0;i<5;++i)tiles[i]=builtin(size,i);return tiles;}
 std::array<std::future<void>,5> tasks;
 for(unsigned worker=0;worker<workers;++worker)tasks[worker]=std::async(std::launch::async,[&,worker]{
  for(unsigned i=worker;i<tiles.size();i+=workers)tiles[i]=builtin(size,int(i));
 });
 // get() propagates failures; the remaining futures join before tiles die.
 for(unsigned worker=0;worker<workers;++worker)tasks[worker].get();
 return tiles;
}
}
