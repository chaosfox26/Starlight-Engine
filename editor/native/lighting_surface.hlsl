#include "surface_lighting.hlsl"
#include "shadow_surface.hlsl"
float3 brdf(float3 n,float3 v,float3 l,float3 albedo,float rough,float metal){
 SlDirectLighting response=slDirectLighting(n,v,l,albedo,rough,metal);return response.diffuse+response.specular;
}
float spotAtt(int i,float3 wp,out float3 l){
 float3 delta=spots[i].pos.xyz-wp;float distance=length(delta);l=delta/max(distance,.001);
 return slPointAttenuation(distance,spots[i].pos.w)*slSpotCone(spots[i].dir.xyz,l,spots[i].dir.w,spots[i].color.w);
}
float3 shadeLin(float3 wp,float3 n,float3 albedo,float rough,float metal,float ao,float emissive){
 float3 v=normalize(camPos.xyz-wp);
 float3 gradientX=ddx(wp),gradientY=ddy(wp);
 float3 c=brdf(n,v,-normalize(lightDir.xyz),albedo,rough,metal)*slSolarRadiance(lightDir)*slSunVisibility(wp,n);
 c+=slEnvironmentDiffuse(albedo*(1-metal),n,ambient.rgb,ao);
 c+=slEnvironmentSpecular(n,v,ambient.rgb,rough,lerp(.04.xxx,albedo,metal),ao,lightDir,weather.y);
 [loop]for(int i=0;i<32;++i){float3 l;float attenuation=spotAtt(i,wp,l);if(attenuation>0)c+=brdf(n,v,l,albedo,rough,metal)*spots[i].color.rgb*attenuation*slLocalVisibility(i,wp,n,gradientX,gradientY);}
 return lerp(c,albedo,emissive);
}
