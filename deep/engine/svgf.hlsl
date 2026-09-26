// Shared image slots: 0 composite, 1 normal/roughness, 2 diffuse albedo,
// 3 specular albedo, 4 linear depth, 5 pixel motion/expected previous depth/valid,
// 6 primary emission, 7/8 demodulated diffuse/specular, 9/10 clamped signals,
// 11..14 and 15..18 history (diffuse,length; specular,length; moments; normal/depth),
// 19/20 and 21/22 wavelet ping/pong, 23 final linear HDR.
float luminance(float3 c){return dot(c,float3(.2126,.7152,.0722));}
int2 bounded(int2 p){return clamp(p,0,int2(frame.xy)-1);}
float geometryWeight(int2 a,int2 b){
 float za=images[4][a].x,zb=images[4][b].x;
 float3 na=images[1][a].xyz,nb=images[1][b].xyz;
 if(za>=399&&zb>=399)return 1;
 return exp(-abs(za-zb)/max(.015,za*.006))*pow(saturate(dot(na,nb)),48);
}
[numthreads(8,8,1)] void Prefilter(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=(uint2)frame.xy))return;int2 p=id.xy;
 for(uint channel=0;channel<2;channel++){
  float3 center=images[7+channel][p].rgb;float sum=0,square=0,weight=0;
  // Exclude the center from the statistics so an isolated outlier cannot set its own threshold.
  for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){if(x==0&&y==0)continue;int2 q=bounded(p+int2(x,y));float w=geometryWeight(p,q);float l=luminance(images[7+channel][q].rgb);sum+=l*w;square+=l*l*w;weight+=w;}
  float mean=sum/max(weight,.001),variance=max(0,square/max(weight,.001)-mean*mean);
  float bound=mean+3*sqrt(variance)+max(.05,mean*.1),l=luminance(center);
  if(weight>2&&l>bound)center*=bound/max(l,.0001);
  targets[9+channel][p]=float4(min(center,60000),1);
 }
}
[numthreads(8,8,1)] void Temporal(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=(uint2)frame.xy))return;int2 p=id.xy;uint current=11+filterPass.y*4,previous=11+(1-filterPass.y)*4;
 float4 motion=images[5][p];float2 oldPixel=float2(p)+motion.xy;int2 old=int2(floor(oldPixel+.5));
 bool valid=sequence.y==0&&motion.w>.5&&all(old>=0)&&all(old<int2(frame.xy));
 float4 guide=valid?images[previous+3][old]:0;float3 n=images[1][p].xyz;float z=images[4][p].x;
 valid=valid&&images[previous+1][old].a==motion.w&&abs(guide.w-motion.z)<max(.025,motion.z*.012)&&(dot(n,guide.xyz)>.92||(z>=399&&guide.w>=399));
 float4 moments=valid?images[previous+2][old]:0;float4 newMoments=0;
 for(uint channel=0;channel<2;channel++){
  float3 sample=images[9+channel][p].rgb;float4 prior=valid?images[previous+channel][old]:0;
  float3 lo=sample,hi=sample;float mean=0,square=0,weight=0;
  for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){int2 q=bounded(p+int2(x,y));float w=geometryWeight(p,q);float3 v=images[9+channel][q].rgb;float l=luminance(v);mean+=l*w;square+=l*l*w;weight+=w;if(w>.1){lo=min(lo,v);hi=max(hi,v);}}
  mean/=max(weight,.001);float sigma=sqrt(max(0,square/max(weight,.001)-mean*mean));
  prior.rgb=clamp(prior.rgb,lo-sigma*.5,hi+sigma*.5);
  // Animated illumination remains reactive even when geometry reprojection succeeds.
  float difference=abs(luminance(prior.rgb)-luminance(sample))/max(.15,max(luminance(prior.rgb),luminance(sample)));
  float limit=sequence.z!=0?12:64;float length=valid?min(images[previous][old].a+1,limit):1;
  float alpha=max(1/length,sequence.z!=0?saturate(difference-.25)*.35:0);
  float3 value=lerp(prior.rgb,sample,alpha);float l=luminance(sample);
  float2 oldMom=channel==0?moments.xy:moments.zw;float2 m=lerp(oldMom,float2(l,l*l),alpha);
  if(channel==0)newMoments.xy=m;else newMoments.zw=m;
  targets[current+channel][p]=float4(max(value,0),channel==0?length:motion.w);
 }
 targets[current+2][p]=newMoments;targets[current+3][p]=float4(n,z);
}
[numthreads(8,8,1)] void Atrous(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=(uint2)frame.xy))return;int2 p=id.xy;uint iteration=filterPass.x,stepSize=1u<<iteration;
 uint current=11+filterPass.y*4,input=iteration==0?current:(iteration==1?19:21),dest=iteration==1?21:19;
 float4 m=images[current+2][p];float kernel[5]={1,4,6,4,1};
 for(uint channel=0;channel<2;channel++){
  float3 center=images[input+channel][p].rgb;float variance=channel==0?max(0,m.y-m.x*m.x):max(0,m.w-m.z*m.z);
  float sigma=max(.025,sqrt(variance)+luminance(center)*.025);float3 sum=0;float weight=0;
  for(int y=-2;y<=2;y++)for(int x=-2;x<=2;x++){
   int2 q=bounded(p+int2(x,y)*stepSize);float3 value=images[input+channel][q].rgb;
   float w=kernel[x+2]*kernel[y+2]*geometryWeight(p,q)*exp(-abs(luminance(value)-luminance(center))/(4*sigma+.001));
   sum+=value*w;weight+=w;
  }targets[dest+channel][p]=float4(sum/max(weight,.001),1);
 }
}
[numthreads(8,8,1)] void Composite(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=(uint2)frame.xy))return;int2 p=id.xy;
 float3 color=images[19][p].rgb*images[2][p].rgb+images[20][p].rgb*images[3][p].rgb+images[6][p].rgb;
 targets[23][p]=float4(min(max(color,0),60000),1);
}
