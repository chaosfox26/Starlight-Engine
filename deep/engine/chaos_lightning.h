#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
namespace starlight {
struct ArcPoint { float x,y,z; };
struct ArcVertex { float x,y,z,edge; };
inline std::vector<ArcVertex> Lightning(ArcPoint target,float spread,uint32_t seed=8241) {
 std::vector<ArcVertex> out;
 if(!seed)seed=8241;   // xorshift stays at 0 forever, which made every bolt with seed 0 the same straight, unjittered line
 auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return (float)(seed&65535)/32767.5f-1;};
 auto emit=[&](ArcPoint a,ArcPoint b,float width){
  float dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z,len=sqrtf(dx*dx+dy*dy+dz*dz);if(len<1e-6f)return;
  // Crossed ribbons remain visible from any orbit-camera angle.
  for(int plane=0;plane<3;plane++){
   float ux=plane==0?0:plane==1?-dz:dy,uy=plane==0?dz:plane==1?0:-dx,uz=plane==0?-dy:plane==1?dx:0;
   float n=sqrtf(ux*ux+uy*uy+uz*uz);if(n<1e-5f)continue;ux*=width/n;uy*=width/n;uz*=width/n;
   ArcVertex v[]={{a.x-ux,a.y-uy,a.z-uz,-1},{b.x-ux,b.y-uy,b.z-uz,-1},{b.x+ux,b.y+uy,b.z+uz,1},{a.x+ux,a.y+uy,a.z+uz,1}};
   for(int i:{0,1,2,0,2,3})out.push_back(v[i]);
  }
 };
 auto subdivide=[&](auto&& self,ArcPoint a,ArcPoint b,int depth,float jitter,float width)->void{
  if(!depth){emit(a,b,width);return;}
  ArcPoint mid{(a.x+b.x)*.5f+random()*jitter,(a.y+b.y)*.5f+random()*jitter*.35f,(a.z+b.z)*.5f+random()*jitter};
  self(self,a,mid,depth-1,jitter*.52f,width);self(self,mid,b,depth-1,jitter*.52f,width);
  if(depth==3&&spread>0){ArcPoint end{mid.x+random()*spread,mid.y-std::abs(random())*spread,mid.z+random()*spread};self(self,mid,end,2,jitter*.6f,width*.48f);}
 };
 subdivide(subdivide,{0,3,0},target,6,spread*.34f,.035f);return out;
}
}
