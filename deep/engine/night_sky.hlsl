#ifndef STARLIGHT_NIGHT_SKY
#define STARLIGHT_NIGHT_SKY
uint slStarBits(uint seed){seed^=seed>>16;seed*=2246822519u;seed^=seed>>13;seed*=3266489917u;return seed^(seed>>16);}
float slStarRandom(uint seed){return (slStarBits(seed)&16777215u)/16777216.;}
float3 slStarFace(float3 direction,int axis,float footprint){
 float major=direction[axis];if(abs(major)<.45)return 0;
 float3 projected=axis==0?direction.yzx:(axis==1?direction.xzy:direction.xyz);
 float2 uv=projected.xy/abs(major);
 // Adjacent charts are evaluated near a cube edge. Each catalogue point is
 // owned by one chart, so there is no clipped star or doubled seam there.
 const float cells=200;float2 location=(uv*.5+.5)*cells;int2 tile=int2(floor(location));
 float3 radiance=0;
 // Keep the nine catalogue candidates in a bounded loop. Expanding both loops
 // duplicates this path across every surface shader and delays first-frame
 // startup; the loop retains the exact same points, filtering and flux.
 [loop]for(int y=-1;y<=1;++y)[loop]for(int x=-1;x<=1;++x){
  int2 cell=tile+int2(x,y);if(any(cell<0)||any(cell>=int(cells)))continue;
  uint face=uint(axis*2+(major<0));uint seed=uint(cell.x)*73856093u^uint(cell.y)*19349663u^(face+1)*83492791u;
  float2 jitter=float2(slStarRandom(seed+17),slStarRandom(seed+71));
  float2 coordinate=(float2(cell)+jitter)/cells*2-1;
  // Cube-chart area compensation avoids a dense square patch at face centres.
  float probability=.09*pow(1+dot(coordinate,coordinate),-1.5);
  if(slStarRandom(seed+113)>=probability)continue;
  float3 chartPoint=float3(coordinate,major<0?-1:1);
  float3 star=normalize(axis==0?chartPoint.zxy:(axis==1?chartPoint.xzy:chartPoint.xyz));
  float separation=length(cross(direction,star));
  float radius=.00008+.00016*pow(slStarRandom(seed+199),3);
  float variance=radius*radius+footprint*footprint*.25;
  if(separation*separation>variance*12)continue;
  // A normalized Gaussian footprint filter conserves flux as resolution and
  // camera angle change. No screen-space seed or frame-varying sparkle.
  float coverage=radius*radius/variance*exp(-separation*separation/(2*variance));
  float brightness=.08+6*pow(slStarRandom(seed+251),5);
  float temperature=slStarRandom(seed+313);
  float3 color=temperature<.3?lerp(float3(1,.53,.28),float3(1,.88,.7),temperature/.3):lerp(float3(1,.94,.83),float3(.62,.78,1),(temperature-.3)/.7);
  radiance+=color*brightness*coverage;
 }
 return radiance;
}
float3 slNightStars(float3 direction,float footprint){
 // Rough materials receive the smooth night environment, not unresolved
 // catalogue specks. Clear water and the background retain filtered stars.
 if(direction.y<-.02||footprint>=.0035)return 0;
 footprint=max(.00003,footprint);
 return (slStarFace(direction,0,footprint)+slStarFace(direction,1,footprint)+slStarFace(direction,2,footprint))*smoothstep(-.02,.03,direction.y)*(1-smoothstep(.0015,.0035,footprint));
}
#endif
