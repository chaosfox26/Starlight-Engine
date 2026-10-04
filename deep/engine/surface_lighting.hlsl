#ifndef STARLIGHT_SURFACE_LIGHTING
#define STARLIGHT_SURFACE_LIGHTING
// Linear-light, energy-conserving surface response shared by raster and DXR.
// Returned terms already contain N.L; visibility belongs to the caller.
struct SlDirectLighting {float3 diffuse;float3 specular;};
SlDirectLighting slDirectLighting(float3 n,float3 v,float3 l,float3 albedo,float rough,float metal){
 SlDirectLighting result;result.diffuse=0;result.specular=0;
 float nl=saturate(dot(n,l)),nv=saturate(dot(n,v));if(nl<=0||nv<=0)return result;
 float3 h=v+l;h*=rsqrt(max(dot(h,h),.000001));
 float nh=saturate(dot(n,h)),vh=saturate(dot(v,h)),lh=saturate(dot(l,h));
 rough=clamp(rough,.045,1);metal=saturate(metal);float alpha=rough*rough,a2=alpha*alpha;
 float denominator=nh*nh*(a2-1)+1;
 float distribution=a2/max(3.141592654*denominator*denominator,.0000001);
 // Height-correlated Smith visibility: stable at grazing angles.
 float gv=nl*sqrt(max(nv*nv*(1-a2)+a2,.0000001));
 float gl=nv*sqrt(max(nl*nl*(1-a2)+a2,.0000001));
 float visibility=.5/max(gv+gl,.00001);
 float3 f0=lerp(.04.xxx,max(albedo,0),metal),f=f0+(1-f0)*pow(1-vh,5);
 float grazing=.5+2*rough*lh*lh;
 float scatter=(1+(grazing-1)*pow(1-nl,5))*(1+(grazing-1)*pow(1-nv,5));
 result.diffuse=(1-f)*(1-metal)*max(albedo,0)*(scatter/3.141592654)*nl;
 result.specular=distribution*visibility*f*nl;return result;
}
float slPointAttenuation(float distance,float range){
 if(range<=0)return 0;float ratio=saturate(distance/range);
 float window=saturate(1-ratio*ratio*ratio*ratio);
 // A finite source radius prevents singularities at a light's origin.
 return window*window/(1+distance*distance);
}
float slSpotCone(float3 direction,float3 toLight,float outer,float inner){
 if(outer<-1)return 1;
 float cosine=dot(-toLight,direction*rsqrt(max(dot(direction,direction),.000001)));
 float t=saturate((cosine-outer)/max(inner-outer,.00001));return t*t*(3-2*t);
}
#include "sky_radiance.hlsl"
float3 slEnvironmentDiffuse(float3 albedo,float3 normal,float3 ambient,float ao){
 return max(albedo,0)*max(ambient,0)*lerp(.12,.22,saturate(normal.y*.5+.5))*saturate(ao);
}
float3 slEnvironmentSpecular(float3 normal,float3 view,float3 ambient,float rough,float3 f0,float ao,float4 sun,float storm){
 float nv=saturate(dot(normal,view));float3 f=f0+(max((1-rough).xxx,f0)-f0)*pow(1-nv,5);
 float occlusion=saturate(pow(nv+saturate(ao),exp2(-16*rough-1))-1+saturate(ao));
 return slSkyRadiance(reflect(-view,normal),ambient,sun,max(.01,rough*rough*.4),storm)*f*lerp(1,.35,rough)*occlusion;
}
#endif
