#define SL_SURFACE_ALBEDO t8
#define SL_SURFACE_NORMAL t9
#define SL_SURFACE_ORM t10
#define SL_SURFACE_PIXEL
#include "surface_material.hlsl"
#include "foliage_material.hlsl"
Texture2D<float4> slOpaqueColor : register(t11);
Texture2D<float> slOpaqueDepth : register(t12);
SamplerState slScreenSampler : register(s2);
float3 slOpaqueWorld(float2 uv,float depth){float4 p=mul(float4(uv*float2(2,-2)+float2(-1,1),depth,1),inverseViewProjection);return p.xyz/max(.000001,p.w);}
float3 slWaterOpaquePoint(int2 pixel,float2 dimensions,out float depth){
 pixel=clamp(pixel,int2(0,0),int2(dimensions)-1);depth=slOpaqueDepth.Load(int3(pixel,0));
 return slOpaqueWorld((float2(pixel)+.5)/dimensions,depth);
}
float slWaterReceiverSun(float3 p,float3 normal,float3 dx,float3 dy){
 // Use the reconstructed opaque receiver plane, rather than derivatives of
 // water geometry or a distorted depth quad crossing a foreground silhouette.
 if(shadowTexel.w<.5||params.y>=2)return 1;
 float depth=mul(float4(p,1),viewProjection).w;
 if(depth<=0||depth>=shadowCuts.z)return 1;
 int cascade=depth<shadowCuts.x?0:depth<shadowCuts.y?1:2;
 float visibility=slSunCascade(p,normal,dx,dy,cascade);
 float end=shadowCuts[cascade],start=cascade==0?0:shadowCuts[cascade-1];
 float blend=smoothstep(end-(end-start)*.12,end,depth);
 if(cascade<2&&blend>0)visibility=lerp(visibility,slSunCascade(p,normal,dx,dy,cascade+1),blend);
 if(cascade==2)visibility=lerp(visibility,1,blend);
 return visibility;
}
float3 slWaterReceiverCaustics(float2 uv,float2 dimensions,float3 waterPoint,float3 waterNormal,float waterDepth,float time,float surfaceFootprint,float3 under){
 int2 pixel=int2(uv*dimensions);float depth,depthXp,depthXm,depthYp,depthYm;
 float3 receiver=slWaterOpaquePoint(pixel,dimensions,depth);
 float3 xp=slWaterOpaquePoint(pixel+int2(1,0),dimensions,depthXp)-receiver;
 float3 xm=receiver-slWaterOpaquePoint(pixel-int2(1,0),dimensions,depthXm);
 float3 yp=slWaterOpaquePoint(pixel+int2(0,1),dimensions,depthYp)-receiver;
 float3 ym=receiver-slWaterOpaquePoint(pixel-int2(0,1),dimensions,depthYm);
 float3 dx=dot(xp,xp)<dot(xm,xm)?xp:xm,dy=dot(yp,yp)<dot(ym,ym)?yp:ym;
 float3 plane=cross(dx,dy);float area=length(plane);
 float verticalDepth=waterPoint.y-receiver.y,submersion=dot(waterPoint-receiver,waterNormal);
 float expectedFootprint=max(.002,surfaceFootprint*(1+length(receiver-waterPoint)/max(.1,length(waterPoint-camPos.xyz))));
 // Empty sky, foreground objects, dry receivers and broken depth edges never
 // acquire light nets. The authored depth saturates at two metres, so only
 // unsaturated values provide an upper bound; actual receiver depth remains
 // authoritative for the sunlight path and deep-water attenuation.
 float receiverLimit=max(.08,expectedFootprint*6);
 bool belowBed=waterDepth<1.99&&verticalDepth>waterDepth+.15+expectedFootprint*3;
 if(depth>=.999999||waterNormal.y<.55||submersion<=.018||verticalDepth<=.018||waterDepth<=.02||belowBed||area<1e-10||length(dx)>receiverLimit||length(dy)>receiverLimit)return 0;
 float3 normal=plane/area;if(dot(normal,camPos.xyz-receiver)<0)normal=-normal;
 float3 light=-normalize(lightDir.xyz),incoming=refract(-light,waterNormal,1/1.333);
 float sunHeight=smoothstep(.02,.16,light.y),facing=saturate(dot(normal,-incoming));
 float crossing=max(.1,-dot(incoming,waterNormal)),path=submersion/crossing;
 float3 entry=receiver-incoming*path;
 float3 entryDx=dx+incoming*dot(dx,waterNormal)/crossing,entryDy=dy+incoming*dot(dy,waterNormal)/crossing;
 float footprint=max(length(entryDx),length(entryDy));
 float pattern=slWaterCaustics(entry.xz,time,footprint);
 float visibility=slWaterReceiverSun(receiver,normal,dx,dy);
 return max(0,under)*slSolarRadiance(lightDir)*slWaterTransmission(path)*pattern*(.35*facing*sunHeight*visibility)*(1-smoothstep(3,9,submersion));
}
float slReflectionDistance(float3 p,out float2 uv){
 float4 projected=mul(float4(p,1),viewProjection);uv=projected.xy/max(.000001,projected.w)*float2(.5,-.5)+.5;
 if(projected.w<=0||any(uv<=0)||any(uv>=1))return 10000;
 float depth=slOpaqueDepth.SampleLevel(slScreenSampler,uv,0);if(depth>=.999999)return 10000;
 return length(slOpaqueWorld(uv,depth)-camPos.xyz)-length(p-camPos.xyz);
}
float3 slScreenReflection(float3 origin,float3 direction,float3 fallback,float rough){
 if(motionTime.w<.5)return fallback;
 // Opaque scene copies exclude water, preventing recursive/self reflections.
 // Bracket the first depth crossing and refine it; deterministic steps avoid
 // sparkling when waves move. Missing/offscreen rays retain sky radiance.
 float previous=.05,previousGap=10000;
 [loop]for(int step=0;step<32;++step){
  float distance=.08+step*.15+step*step*.012;float2 uv;
  float gap=slReflectionDistance(origin+direction*distance,uv);
  if(gap<0&&previousGap>0){
   float low=previous,high=distance;
   [unroll]for(int refine=0;refine<5;++refine){float middle=(low+high)*.5;float2 hitUv;float d=slReflectionDistance(origin+direction*middle,hitUv);if(d>0)low=middle;else high=middle;}
   gap=slReflectionDistance(origin+direction*((low+high)*.5),uv);
   float confidence=saturate(1-abs(gap)/(.06+distance*.015));
   float edge=min(min(uv.x,1-uv.x),min(uv.y,1-uv.y));confidence*=smoothstep(0,.075,edge)*(1-rough)*saturate(1-distance/18);
   return lerp(fallback,max(0,slOpaqueColor.SampleLevel(slScreenSampler,uv,0).rgb),confidence);
  }
  previous=distance;previousGap=gap;
 }
 return fallback;
}
bool slWaterRefractedHit(float3 origin,float3 direction,float2 dimensions,out float2 hitUv){
 // Follow the transmitted ray to its first opaque depth crossing. A single
 // displaced colour lookup can pull an unrelated dry bank into the water.
 hitUv=0;float previous=.005,previousGap=10000;
 [loop]for(int step=0;step<32;++step){
  float distance=.015+step*.08+step*step*.012;float2 uv;
  float gap=slReflectionDistance(origin+direction*distance,uv);
  if(gap<0&&previousGap>0){
   float low=previous,high=distance;
   [unroll]for(int refine=0;refine<6;++refine){float middle=(low+high)*.5;float2 probe;float d=slReflectionDistance(origin+direction*middle,probe);if(d>0)low=middle;else high=middle;}
   float travel=(low+high)*.5;gap=slReflectionDistance(origin+direction*travel,uv);
   float depth;float3 receiver=slWaterOpaquePoint(int2(uv*dimensions),dimensions,depth);
   // A silhouette discontinuity is not an intersection; retain undistorted
   // transmission when scene copies cannot represent the refracted surface.
   if(depth<.999999&&receiver.y<origin.y-.005&&abs(gap)<.04+travel*.008&&length(receiver-(origin+direction*travel))<.08+travel*.02){hitUv=uv;return true;}
   return false;
  }
  previous=distance;previousGap=gap;
 }
 return false;
}
float hash31(float3 p) { p=frac(p*.1031);p+=dot(p,p.yzx+33.33);return frac((p.x+p.y)*p.z); }
float noise3(float3 p) { float3 a=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(lerp(hash31(a),hash31(a+float3(1,0,0)),f.x),lerp(hash31(a+float3(0,1,0)),hash31(a+float3(1,1,0)),f.x),f.y),lerp(lerp(hash31(a+float3(0,0,1)),hash31(a+float3(1,0,1)),f.x),lerp(hash31(a+float3(0,1,1)),hash31(a+float3(1,1,1)),f.x),f.y),f.z); }
float detailNoise(float3 p) {float footprint=max(length(ddx(p)),length(ddy(p)));return lerp(noise3(p),.5,saturate(footprint-.4));}
float relief(float3 p,float k){return detailNoise(p*5)*.58+detailNoise(p*18)*.28+detailNoise(p*65)*.14;}
PSOut PSMain(VSOut i) {
 PSOut po;float2 cn=i.cur.xy/i.cur.w,pn=i.prv.xy/i.prv.w;po.mvec=(pn-cn)*mvScale.xy;
 float3 n=normalize(i.wn),faceNormal=cross(ddx(i.wp),ddy(i.wp));if(dot(faceNormal,n)*dot(faceNormal,camPos.xyz-i.wp)<0)n=-n;
 float3 geometryNormal=n;
 float kind=slSurfaceKind(i.surface.w),time=params.w;float3 color=baseColor.rgb*i.surface.rgb;
 float rough=.78,emission=baseColor.a,ao=1;float3 wp=i.wp,linearColor=toLin(color);
 if(slBarkSurface(i.surface.w)||slLeafSurface(i.surface.w)){
  float foliageFootprint=max(length(ddx(wp)),length(ddy(wp)));
  SlSurfaceMaterial material;
  if(slBarkSurface(i.surface.w))material=slBarkMaterial(wp,n,i.surface.rgb,foliageFootprint);else material=slLeafMaterial(wp,n,float3(i.surface.xy,i.foliageBits));
  float3 foliageColor=toLin(baseColor.rgb)*material.tint;
  float3 lit=shadeLin(wp,material.normal,foliageColor,material.roughness,0,material.ao,0);
  if(slLeafSurface(i.surface.w))lit+=slLeafThinLight(foliageColor,geometryNormal,normalize(camPos.xyz-wp),-normalize(lightDir.xyz),i.foliageBits)*slSolarRadiance(lightDir)*slSunVisibility(wp,geometryNormal);
  po.color=float4(finish(wp,lit),1);return po;
 }
 if(slGrassBlade(i.surface.w)){
  SlSurfaceMaterial material=slGrassBladeMaterial(i.surface.rgb,n,max(length(ddx(wp)),length(ddy(wp))));
  linearColor=toLin(baseColor.rgb)*material.tint;n=material.normal;rough=material.roughness;ao=material.ao;
 }else if(kind>.5&&kind<6.8){
  float footprint=max(length(ddx(wp)),length(ddy(wp)));
  SlSurfaceMaterial material=slSurfaceMaterial(wp,n,kind,footprint);
  linearColor*=material.tint;n=material.normal;rough=material.roughness;ao=material.ao;
 }
 float footprint=max(length(ddx(wp)),length(ddy(wp)));
 float wet=kind<6.8?slWeatherExposure(wp,geometryNormal,weather,weatherLow,weatherHigh):0;
 if(kind<6.8)slWetSurface(wp,geometryNormal,kind,wet,time,footprint,linearColor,n,rough);
 float3 v=normalize(camPos.xyz-wp),l=-normalize(lightDir.xyz);
 float3 result;
 if(kind>6.8&&kind<7.5){
  float footprint=max(length(ddx(wp)),length(ddy(wp)));
  bool droplet=slWaterDroplet(kind);SlWaterMaterial water;
  if(droplet)water=slWaterDropMaterial(n);else water=slWaterMaterial(wp,n,time,i.surface.x*2,footprint);n=water.normal;
  float fresnel=.0204+.9796*pow(1-saturate(dot(n,v)),5);
  float3 reflection=reflect(-v,n),sky=slSkyRadiance(reflection,ambient.rgb,lightDir,max(.0005,water.roughness*water.roughness*.4),weather.y);
  if(params.y<2&&!droplet)sky=slScreenReflection(wp+n*.015,reflection,sky,water.roughness);
  float3 sparkle=pow(saturate(dot(reflect(-l,n),v)),lerp(260,24,water.roughness))*slSolarRadiance(lightDir)*slSunVisibility(wp,geometryNormal);
  result=lerp(water.color,sky,fresnel*(1-water.foam)) + sparkle*(1-water.foam)*.65;
  if(params.y<2){
   float2 dimensions=float2(mvScale.x*2,-mvScale.y*2),uv=i.pos.xy/dimensions;
   float sceneDepth=slOpaqueDepth.SampleLevel(slScreenSampler,uv,0);float3 bottom=slOpaqueWorld(uv,sceneDepth);
   float thickness=droplet?max(.001,i.surface.y):clamp(length(bottom-wp),0,30);float3 through=refract(-v,n,1/1.333);
   float4 projected=mul(float4(wp+through*thickness,1),viewProjection);float2 shifted=projected.xy/max(.0001,projected.w)*float2(.5,-.5)+.5;
   // Blend the distortion out before its sample leaves the available image.
   // A hard offscreen fallback makes a visible moving stripe across water.
   float2 distortion=(shifted-uv)*dimensions;
   float2 room=float2(distortion.x>=0?1-uv.x:uv.x,distortion.y>=0?1-uv.y:uv.y)*dimensions;
   float2 bounded=distortion*room/max(room+abs(distortion),.0001);
   float2 edgeWeight=smoothstep(0,96,room-abs(distortion));
   shifted=projected.w>0?uv+lerp(bounded,distortion,edgeWeight)/dimensions:uv;
   float refractedDepth;float3 refractedReceiver=slWaterOpaquePoint(int2(shifted*dimensions),dimensions,refractedDepth);
   // Raw texel depth never interpolates a sky/foreground edge into a receiver.
   // Compare against the water plane at the shifted ray, rather than using the
   // original pixel's depth on a different ray across a sloping wave surface.
   if(droplet?refractedDepth<i.pos.z-.00005:(refractedDepth<.999999&&(refractedReceiver.y>=wp.y-.005||dot(wp-refractedReceiver,geometryNormal)<=.005)))shifted=uv;
   if(!droplet){float2 hitUv;shifted=slWaterRefractedHit(wp,through,dimensions,hitUv)?hitUv:uv;}
   float3 under=slOpaqueColor.SampleLevel(slScreenSampler,shifted,0).rgb;
   if(!droplet){
    float receiverDepth;float3 receiver=slWaterOpaquePoint(int2(shifted*dimensions),dimensions,receiverDepth);
    thickness=clamp(length(receiver-wp),0,30);
    under+=slWaterReceiverCaustics(shifted,dimensions,wp,geometryNormal,i.surface.x*2,time,footprint,under);
   }
   float3 transmission=slWaterTransmission(thickness);under=under*transmission+float3(.008,.04,.065)*(1-transmission);
   result=under*(1-fresnel)*(1-water.foam)+sky*fresnel*(1-water.foam)+shadeLin(wp,n,float3(.58,.66,.64),.7,0,1,0)*water.foam+sparkle*(1-water.foam)*.65;
  }
  po.color=float4(finish(wp,result),1);return po;
 }
 if(kind>8.5&&kind<9.5){
  float3 surface=i.surface.rgb;if(kind<9.3)surface.y=noise3(wp*2+time);
  result=slLightningEmission(kind,surface,n,camPos.xyz-wp,time);
  po.color=float4(result,1);return po;
 }
 if(kind>9.5){
  SlMagicStoneMaterial magic=slMagicStoneMaterial(wp,n,kind,linearColor,time,max(length(ddx(wp)),length(ddy(wp))));
  result=shadeLin(wp,magic.normal,magic.color,magic.roughness,0,magic.ao,0)+magic.emission;
  po.color=float4(result,1);return po;
 }
 if(kind>7.5){
  SlLavaMaterial lava=slLavaMaterial(wp,n,time,max(length(ddx(wp)),length(ddy(wp))),kind,i.surface.rgb);
  result=shadeLin(wp,lava.normal,lava.color,lava.roughness,0,1,0)+lava.emission;
  po.color=float4(finish(wp,result),1);return po;
 }
 result=shadeLin(wp,n,linearColor,rough,0,ao,emission);
 if(wet>0){float3 reflected=reflect(-v,n);float fresnel=.04+.96*pow(1-saturate(dot(n,v)),5);result+=ambient.rgb*lerp(.25,1.7,saturate(reflected.y*.5+.5))*fresnel*wet*(1-rough);}
 if(slGrassBlade(i.surface.w))result+=slGrassThinLight(linearColor,n,v,l)*slSolarRadiance(lightDir)*slSunVisibility(wp,geometryNormal);
 po.color=float4(finish(wp,result),1);return po;
}
