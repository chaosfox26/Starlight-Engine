#ifndef STARLIGHT_SKY_RADIANCE
#define STARLIGHT_SKY_RADIANCE
#include "night_sky.hlsl"
#include "solar_radiance.hlsl"
// Bounded analytic single-scattering approximation. Optical depths use metre
// scale heights; curved-horizon air mass stays finite at grazing directions.
// This is a clear-sky model, not multiple scattering or a cloud simulation.
float slSkyAirMass(float elevation){
 return rsqrt(max(elevation,0)*max(elevation,0)+.0006);
}
float3 slSkyRadiance(float3 direction,float3 ambient,float4 sunlight,float footprint,float storm){
 direction*=rsqrt(max(dot(direction,direction),.000001));
 float3 sun=-sunlight.xyz*rsqrt(max(dot(sunlight.xyz,sunlight.xyz),.000001));
 float cosine=clamp(dot(direction,sun),-1,1);
 float rayPhase=.0596831*(1+cosine*cosine),g=.76;
 // Widen the forward lobe when evaluating a rough reflection, conserving
 // its angular behaviour instead of reflecting a razor-sharp solar point.
 g=lerp(g,.25,saturate(footprint*3));
 float miePhase=(1-g*g)/max(.001,12.56637*pow(1+g*g-2*g*cosine,1.5));
 float3 rayDepth=float3(.0464,.108,.2648);
 float3 mieDepth=.0252.xxx;
 float viewMass=slSkyAirMass(direction.y),sunMass=slSkyAirMass(sun.y);
 float3 depth=(rayDepth+mieDepth)*viewMass;
 // Molecular scattering higher above the observer has a shorter solar path
 // than low aerosol haze. A single shared attenuation turns dusk green.
 float altitudePath=.18+.82*exp(-max(direction.y,0)*5);
 float3 rayTransmission=exp(-(rayDepth*altitudePath+mieDepth*.1)*sunMass);
 float3 mieTransmission=exp(-(rayDepth+mieDepth)*sunMass);
 float3 scatter=(rayDepth*rayPhase*rayTransmission+mieDepth*miePhase*mieTransmission)/(rayDepth+mieDepth)*(1-exp(-depth));
 float daylight=smoothstep(-.12,.035,sun.y);
 float energy=max(.06*daylight,dot(ambient,float3(.2126,.7152,.0722))-.027);
 float3 sky=scatter*(energy*54)*daylight;
 // The authored ambient colour remains a tint and a night floor. The latter
 // also keeps fantasy night scenes readable without inventing a second sun.
 float3 tint=max(ambient,0)/max(.0001,dot(max(ambient,0),float3(.2126,.7152,.0722)));
 sky*=lerp(float3(1,1,1),tint,.22);
 sky+=max(ambient,0)*lerp(.12,.22,saturate(direction.y))*(1-daylight);
 float horizon=smoothstep(-.08,.025,direction.y);
 float3 ground=max(ambient,0)*.12;
 sky=lerp(ground,sky,horizon);
 // Solar radius is about a quarter degree. Pixel/reflection footprint filters
 // its silhouette; only sky background and clear-water calls use the disk.
 float radius=.00465,angle=acos(cosine),filter=max(.00008,footprint);
 float disk=1-smoothstep(max(0,radius-filter),radius+filter,angle);
 float coverage=min(1,radius*radius/max(radius*radius,filter*filter));
 sky+=float3(1,.96,.87)*exp(-(rayDepth+mieDepth)*rsqrt(1.0006))*slSolarRadiance(sunlight)*disk*coverage*8*horizon;
 float night=1-smoothstep(-.16,-.025,sun.y);
 if(night>0)sky+=slNightStars(direction,footprint)*night*pow(1-saturate(storm),3);
 return max(sky,0);
}
#endif
