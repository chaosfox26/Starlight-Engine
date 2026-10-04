Texture2DArray<float> slSunDepth : register(t13);
TextureCubeArray<float> slLocalDepth : register(t14);
SamplerComparisonState slShadowSampler : register(s3);
// Fixed symmetric taps keep the filter stable across frames. Source-size
// filtering operates on the actual triangle depth maps, never object bounds.
static const float2 slShadowDisk[16]={
 float2(0,0),float2(.527,.085),float2(-.040,.536),float2(-.670,-.179),
 float2(.185,-.650),float2(.683,.499),float2(-.565,.688),float2(-.896,.162),
 float2(-.272,-.898),float2(.872,-.297),float2(.133,.940),float2(-.756,-.544),
 float2(.451,-.877),float2(.934,.153),float2(.432,.680),float2(-.382,-.349)};
float slSunRaw(float2 uv,int cascade){
 if(any(uv<0)||any(uv>1))return 1;
 int resolution=int(round(1/shadowCuts.w));
 return slSunDepth.Load(int4(clamp(int2(uv*resolution),0,resolution-1),cascade,0));
}
float slLocalReceiverDepth(float3 ray,float3 n,float plane,float distance,float4 settings){
 float denominator=dot(ray,n),along=abs(denominator)>.001?plane/denominator:distance;
 float3 hit=ray*max(settings.y,along);float major=max(abs(hit.x),max(abs(hit.y),abs(hit.z)));
 return settings.z/(settings.z-settings.y)-settings.z*settings.y/((settings.z-settings.y)*max(settings.y,major));
}
float slSunCascade(float3 wp,float3 n,float3 gradientX,float3 gradientY,int cascade){
 float texel=shadowTexel[cascade];
 // Receiver offset follows the geometric normal; depth bias is kept small so
 // contact remains attached. Receiver-plane correction also covers wider
 // source-sized filters instead of detaching the shadow with more bias.
 float3 l=-normalize(lightDir.xyz);
 if(dot(n,l)<0)n=-n;
 float4 p=mul(float4(wp+n*texel*(.35+.9*(1-saturate(dot(n,l)))),1),sunShadow[cascade]);
 float3 q=p.xyz/p.w;float2 uv=q.xy*float2(.5,-.5)+.5;
 float3 qdx=mul(float4(gradientX,0),sunShadow[cascade]).xyz,qdy=mul(float4(gradientY,0),sunShadow[cascade]).xyz;
 float2 dx=qdx.xy*float2(.5,-.5),dy=qdy.xy*float2(.5,-.5);float zx=qdx.z,zy=qdy.z;
 float determinant=dx.x*dy.y-dx.y*dy.x;
 float2 slope=abs(determinant)>1e-12?float2(zx*dy.y-zy*dx.y,zy*dx.x-zx*dy.x)/determinant:0;
 // Compare each tap against the receiver plane at that tap, not against the
 // center depth. Account for the comparison sampler's half-texel footprint
 // too. This removes grazing-angle striping without a large floating gap.
 float bias=.00002+.5*shadowCuts.w*(abs(slope.x)+abs(slope.y));
 if(any(uv<0)||any(uv>1)||q.z<0||q.z>1)return 1;
 float radius=shadowCuts.w;
 // Solar angular radius (about a quarter degree): the penumbra grows with
 // caster/receiver separation, rather than with distance from the camera.
 if(shadowCuts.w<=1.0/2048){
  float depthScale=length(float3(sunShadow[cascade]._m02,sunShadow[cascade]._m12,sunShadow[cascade]._m22));
  float search=min(16*shadowCuts.w,.186/max(.001,texel)*shadowCuts.w);
  float gap=0,blockers=0;int taps=shadowCuts.w<=1.0/4096?12:8;
  [loop]for(int i=0;i<taps;++i){
   float2 offset=slShadowDisk[i]*search;float receiver=q.z+dot(slope,offset)-bias;
   float depth=slSunRaw(uv+offset,cascade);
   if(depth<receiver-.00001){gap+=receiver-depth;++blockers;}
  }
  if(blockers==0)return 1;
  float separation=gap/(blockers*max(depthScale,.000001));
  radius=clamp(.00465*separation/max(texel,.001),1,24)*shadowCuts.w;
 }
 if(radius<=shadowCuts.w*1.05){
  float result=0;
  [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x)
   result+=slSunDepth.SampleCmpLevelZero(slShadowSampler,float3(uv+float2(x,y)*shadowCuts.w,cascade),q.z+dot(slope,float2(x,y)*shadowCuts.w)-bias);
  return result/9;
 }
 float result=0;int filterTaps=shadowCuts.w<=1.0/4096?16:12;
 [loop]for(int i=0;i<filterTaps;++i){float2 offset=slShadowDisk[i]*radius;result+=slSunDepth.SampleCmpLevelZero(slShadowSampler,float3(uv+offset,cascade),q.z+dot(slope,offset)-bias);}
 return result/filterTaps;
}
float slSunVisibility(float3 wp,float3 n){
 // Evaluate derivatives before choosing a cascade: adjacent pixels may use
 // different maps, and derivatives inside that branch create visible seams.
 float3 gradientX=ddx(wp),gradientY=ddy(wp),geometric=cross(gradientX,gradientY);
 if(dot(geometric,geometric)>1e-14){geometric=normalize(geometric);n=dot(geometric,n)<0?-geometric:geometric;}
 if(shadowTexel.w<.5||params.y>=2)return 1;
 float depth=mul(float4(wp,1),viewProjection).w;
 if(depth<=0||depth>=shadowCuts.z)return 1;
 int cascade=depth<shadowCuts.x?0:depth<shadowCuts.y?1:2;
 float result=slSunCascade(wp,n,gradientX,gradientY,cascade);
 float end=shadowCuts[cascade],start=cascade==0?0:shadowCuts[cascade-1];
 float transition=smoothstep(end-(end-start)*.12,end,depth);
 if(cascade<2&&transition>0)result=lerp(result,slSunCascade(wp,n,gradientX,gradientY,cascade+1),transition);
 if(cascade==2)result=lerp(result,1,transition);
 return result;
}
float slLocalVisibility(int light,float3 wp,float3 n,float3 gradientX,float3 gradientY){
 float4 settings=localShadow[light];if(settings.x<.5||params.y>=2)return 1;
 float3 origin=spots[light].pos.xyz,delta=wp-origin;float distance=length(delta);
 if(distance<=settings.y||distance>=settings.z)return 1;
 float3 geometric=cross(gradientX,gradientY);
 if(dot(geometric,geometric)>1e-14){geometric=normalize(geometric);n=dot(geometric,n)<0?-geometric:geometric;}
 float3 direction=delta/distance;if(dot(n,direction)>0)n=-n;
 // A light-facing geometric offset covers the comparison sampler footprint,
 // while each outer tap intersects the actual receiver plane. Cube sampling
 // joins the six faces without clamping shadows at face boundaries.
 float texel=distance*2*settings.w;delta+=n*texel*1.15;
 float3 tangent=normalize(cross(abs(direction.y)>.9?float3(0,0,1):float3(0,1,0),direction));float3 bitangent=cross(direction,tangent);
 float plane=dot(delta,n),radius=texel;
 // A bounded six-centimetre source radius gives local lamps a finite emitter.
 // Low quality retains the inexpensive contact filter; Medium/High search
 // blockers and widen only when a real caster stands off the receiver.
 if(settings.w<=1.0/512){
  float sourceRadius=.06,search=min(16*texel,sourceRadius),blockerDistance=0,blockers=0;
  int taps=settings.w<=1.0/1024?12:8;
  [loop]for(int i=0;i<taps;++i){
   float2 offset=slShadowDisk[i]*search;float3 ray=normalize(delta+tangent*offset.x+bitangent*offset.y);
   float receiver=slLocalReceiverDepth(ray,n,plane,distance,settings)-.000015;
   float depth=slLocalDepth.SampleLevel(samp,float4(ray,settings.x-1),0);
   if(depth<receiver-.000015){
    float major=settings.z*settings.y/max(.000001,settings.z-depth*(settings.z-settings.y));
    blockerDistance+=major/max(.0001,max(abs(ray.x),max(abs(ray.y),abs(ray.z))));++blockers;
   }
  }
  if(blockers==0)return 1;
  float blocker=max(settings.y,blockerDistance/blockers);
  radius=clamp(sourceRadius*max(0,distance-blocker)/blocker,texel,24*texel);
 }
 if(radius<=texel*1.05){
  float sum=0;
  [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
   float3 ray=normalize(delta+(tangent*x+bitangent*y)*texel);
   sum+=slLocalDepth.SampleCmpLevelZero(slShadowSampler,float4(ray,settings.x-1),slLocalReceiverDepth(ray,n,plane,distance,settings)-.000015);
  }
  return sum/9;
 }
 float sum=0;int filterTaps=settings.w<=1.0/1024?16:12;
 [loop]for(int i=0;i<filterTaps;++i){float2 offset=slShadowDisk[i]*radius;float3 ray=normalize(delta+tangent*offset.x+bitangent*offset.y);sum+=slLocalDepth.SampleCmpLevelZero(slShadowSampler,float4(ray,settings.x-1),slLocalReceiverDepth(ray,n,plane,distance,settings)-.000015);}
 return sum/filterTaps;
}
