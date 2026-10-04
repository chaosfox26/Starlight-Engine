#ifndef STARLIGHT_FOLIAGE_MATERIAL
#define STARLIGHT_FOLIAGE_MATERIAL
// Authored Surface 12/13 are packed as -40/-41. Native 12/13 remain fire/smoke.
bool slBarkSurface(float tag){return abs(tag+40)<.25;}
bool slLeafSurface(float tag){return abs(tag+41)<.25;}
float4 slLeafControls(float packed){
 uint word=(uint)round(packed);return float4(word&63,(word>>6)&63,(word>>12)&63,(word>>18)&63)/63.;
}
// Exact footprint attenuation for a sinusoid. No unresolved high frequency survives.
float slFoliageWave(float phase,float phaseFootprint){return cos(phase)*exp(-.5*phaseFootprint*phaseFootprint);}
float4 slBarkField(float3 p,float scale,float footprint){
 // Three world planes blended by the geometric normal below; vertical grain
 // remains coherent on vertical limbs. This is world anchored, not branch UV bark.
 float4 warp=slFilteredSurfaceNoise(p,2.1*scale,footprint);
 float4 broad=slFilteredSurfaceNoise(float3(p.x,p.y*.18,p.z),5*scale,footprint);
 float4 pores=slFilteredSurfaceNoise(p,57*scale,footprint);
 float phase=6.2831853*(p.x*15*scale+warp.w*.6);
 float3 phaseGradient=6.2831853*(float3(15*scale,0,0)+warp.xyz*.6);
 float attenuation=exp(-.5*pow(footprint*length(phaseGradient),2));
 float ridge=cos(phase)*attenuation;
 float3 slope=-sin(phase)*phaseGradient*attenuation;
 // Noise derivatives are analytic; apply anisotropic chain rule explicitly.
 broad.xyz*=float3(1,.18,1);
 return float4(slope*.00065+broad.xyz*.0025+pores.xyz*.00022,.55+.18*ridge+.27*(broad.w-.5));
}
SlSurfaceMaterial slBarkMaterial(float3 p,float3 normal,float3 controls,float footprint){
 float scale=clamp(controls.x,.25,4),relief=clamp(controls.y,0,2);
 float3 weights=pow(abs(normal),4);weights/=max(.00001,weights.x+weights.y+weights.z);
 float4 a=slBarkField(p.zyx,scale,footprint),b=slBarkField(p.xzy,scale,footprint),c=slBarkField(p,scale,footprint);
 float field=a.w*weights.x+b.w*weights.y+c.w*weights.z;
 float3 gradient=(a.zyx*weights.x+b.xzy*weights.y+c.xyz*weights.z)*relief;
 gradient-=normal*dot(normal,gradient);gradient*=min(1,.32/max(.00001,length(gradient)));
 float4 fleck=slFilteredSurfaceNoise(p,19*scale,footprint);
 SlSurfaceMaterial m;m.tint=lerp(float3(.62,.55,.46),float3(1.16,1.03,.88),saturate(field));
 m.tint*=lerp(.9,1.06,fleck.w);m.normal=normalize(normal-gradient);
 m.roughness=clamp(controls.z+.08*(.5-field),.45,.98);m.ao=1;
 return m;
}
float3 slFoliageUvGradient(float2 gradient,float2 uv,float3 p,float3 normal){
 float2 a=ddx(uv),b=ddy(uv);float3 x=ddx(p),y=ddy(p);
 float determinant=a.x*b.y-a.y*b.x;
 if(abs(determinant)<1e-10)return 0;
 float3 u=(x*b.y-y*a.y)/determinant,v=(y*a.x-x*b.x)/determinant;
 float uu=dot(u,u),uvDot=dot(u,v),vv=dot(v,v),gram=uu*vv-uvDot*uvDot;
 if(gram<1e-12)return 0;
 float3 result=((gradient.x*vv-gradient.y*uvDot)*u+(gradient.y*uu-gradient.x*uvDot)*v)/gram;
 result-=normal*dot(result,normal);return result*min(1,.18/max(.00001,length(result)));
}
SlSurfaceMaterial slLeafMaterial(float3 p,float3 normal,float3 payload){
 float2 uv=payload.xy;float4 controls=slLeafControls(payload.z);
 float footprint=max(length(ddx(uv)),length(ddy(uv)));
 float ribWidth=.018,width=sqrt(ribWidth*ribWidth+footprint*footprint);
 float q=(uv.x-.5)/width,rib=exp(-q*q)*ribWidth/width;
 float veinPhase=uv.y*62.831853+abs(uv.x-.5)*22;
 float veinFootprint=footprint*66.571,filtered=exp(-.5*veinFootprint*veinFootprint);
 float vein=cos(veinPhase)*filtered;
 float2 gradient=float2(-2*q*rib/width,0)*.00009;
 gradient+=-sin(veinPhase)*filtered*float2(sign(uv.x-.5)*22,62.831853)*(.000012*controls.z);
 SlSurfaceMaterial m;m.normal=normalize(normal-slFoliageUvGradient(gradient,uv,p,normal));
 float variation=lerp(.83,1.13,controls.x),age=smoothstep(.65,1,uv.y);
 m.tint=lerp(float3(.85,1.06,.72),float3(1.12,1.01,.61),age*.25)*variation;
 m.tint*=1+.07*rib+.045*vein*controls.z;m.roughness=lerp(.4,.85,controls.w);m.ao=1;
 return m;
}
float3 slLeafThinLight(float3 albedo,float3 normal,float3 view,float3 light,float packed){
 float strength=slLeafControls(packed).y;
 float back=saturate(-dot(normal,light)),forward=pow(saturate(dot(view,-light)),6);
 // Bounded direct thin-sheet approximation; multiply by actual sun visibility.
 // No emission, alpha cutout, wind or ray-traced transmission is introduced.
 return albedo*strength*(back*.18+forward*.025);
}
#endif
