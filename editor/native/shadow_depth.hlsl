cbuffer ShadowTransform : register(b0) { row_major float4x4 model,shadowMatrix; }
cbuffer ShadowClock : register(b1) { float4 params; }
// Exactly the same deformation as the color pass: wind cannot detach the
// shadow from the grass. Static and CPU-deformed meshes also share this path.
#include "grass_wind.hlsl"
struct ShadowVertex { float3 position:POSITION; float4 surface:COLOR0; };
float4 ShadowDepth(ShadowVertex vertex):SV_POSITION {
 float3 p=environmentPosition(vertex.position,vertex.surface,params.w);
 return mul(mul(float4(p,1),model),shadowMatrix);
}
