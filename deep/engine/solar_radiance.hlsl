#ifndef STARLIGHT_SOLAR_RADIANCE
#define STARLIGHT_SOLAR_RADIANCE
// Shared direct sunlight in linear RGB. The longer atmospheric path near the
// horizon removes blue before red. Reference overhead transmission preserves
// existing authored intensity instead of treating it as extraterrestrial power.
// This bounded clear-sky approximation is not a spectral atmosphere simulation.
float3 slSolarRadiance(float4 sunlight){
 float elevation=-sunlight.y*rsqrt(max(dot(sunlight.xyz,sunlight.xyz),.000001));
 float mass=rsqrt(max(elevation,0)*max(elevation,0)+.0006);
 float reference=rsqrt(1.0006);
 float3 transmission=exp(-float3(.0716,.1332,.2900)*max(0,mass-reference));
 // A finite disk fades continuously through the horizon. Do not leave a
 // directional source illuminating grass, coat tips or mist from underground.
 float visibility=smoothstep(-.00465,.00465,elevation);
 return transmission*(max(0,sunlight.w)*visibility);
}
#endif
