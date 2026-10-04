#ifndef STARLIGHT_SURFACE_MATERIAL
#define STARLIGHT_SURFACE_MATERIAL
Texture2DArray<float4> slSurfaceAlbedo : register(SL_SURFACE_ALBEDO);
Texture2DArray<float4> slSurfaceNormal : register(SL_SURFACE_NORMAL);
Texture2DArray<float4> slSurfaceORM : register(SL_SURFACE_ORM);
SamplerState slSurfaceSampler : register(s1);
float4 slSurfaceSample(Texture2DArray<float4> map,float2 uv,float slice,float footprint){
#ifdef SL_SURFACE_PIXEL
 return map.SampleGrad(slSurfaceSampler,float3(uv,slice),ddx(uv),ddy(uv));
#else
 uint width,height,slices,levels;map.GetDimensions(0,width,height,slices,levels);
 return map.SampleLevel(slSurfaceSampler,float3(uv,slice),clamp(log2(max(.00001,footprint*2*width)),0,float(levels-1)));

#endif
}
float4 slSurfaceLayer(Texture2DArray<float4> map,float2 uv,float layer,float footprint){
 float low=floor(layer),blend=layer-low;float4 value=slSurfaceSample(map,uv,low,footprint);
 if(blend>.001)value=lerp(value,slSurfaceSample(map,uv,min(4,low+1),footprint),blend);
 return value;
}
float slSurfaceHash(float3 p){p=frac(p*.1031);p+=dot(p,p.yzx+33.33);return frac((p.x+p.y)*p.z);}
float4 slSurfaceNoise(float3 p){
 float3 cell=floor(p),f=frac(p),u=f*f*(3-2*f),du=6*f*(1-f);
 float a=slSurfaceHash(cell),b=slSurfaceHash(cell+float3(1,0,0)),c=slSurfaceHash(cell+float3(0,1,0)),d=slSurfaceHash(cell+float3(1,1,0));
 float e=slSurfaceHash(cell+float3(0,0,1)),v=slSurfaceHash(cell+float3(1,0,1)),g=slSurfaceHash(cell+float3(0,1,1)),h=slSurfaceHash(cell+1);
 float x0=lerp(a,b,u.x),x1=lerp(c,d,u.x),x2=lerp(e,v,u.x),x3=lerp(g,h,u.x);
 float gx=lerp(lerp(b-a,d-c,u.y),lerp(v-e,h-g,u.y),u.z)*du.x;
 float gy=lerp(x1-x0,x3-x2,u.z)*du.y;
 float gz=(lerp(x2,x3,u.y)-lerp(x0,x1,u.y))*du.z;
 return float4(gx,gy,gz,lerp(lerp(x0,x1,u.y),lerp(x2,x3,u.y),u.z));
}
float4 slFilteredSurfaceNoise(float3 p,float frequency,float footprint){
 float attenuation=1-smoothstep(.35,1.2,footprint*frequency);
 if(attenuation<=0)return float4(0,0,0,.5);
 float4 value=slSurfaceNoise(p*frequency);
 return float4(value.xyz*frequency*attenuation,lerp(.5,value.w,attenuation));
}
struct SlSurfaceMaterial {float3 tint;float3 normal;float roughness;float ao;};
float slWeatherExposure(float3 p,float3 normal,float4 weather,float4 low[64],float4 high[64]){
 if(weather.x<=0)return 0;
 float3 direction=normalize(float3(-weather.z,9,-weather.w));
 p+=normal*.025;
 [loop]for(int i=0;i<64;i++){
  if(low[i].w<=0)break;
  float nearT=0,farT=1000;bool outside=false;
  [unroll]for(int axis=0;axis<3;axis++){
   if(abs(direction[axis])<.0001){if(p[axis]<low[i][axis]||p[axis]>high[i][axis])outside=true;}
   else{float a=(low[i][axis]-p[axis])/direction[axis],b=(high[i][axis]-p[axis])/direction[axis];nearT=max(nearT,min(a,b));farT=min(farT,max(a,b));}
  }
  if(!outside&&farT>max(.02,nearT))return 0;
 }
 return saturate(weather.x)*saturate(dot(normal,direction)*.7+.3);
}
void slWetSurface(float3 p,float3 geometryNormal,float kind,float wet,float time,float footprint,inout float3 color,inout float3 normal,inout float roughness){
 if(wet<=0)return;
 float pores=slFilteredSurfaceNoise(p,2.7,footprint).w;
 float damp=saturate(wet*(.7+.45*pores));
 float porous=kind>2.5&&kind<3.5?.65:slSurfaceHash(floor(p*.35))*.15+.4;
 color*=lerp(1,1-porous,damp);
 roughness=lerp(roughness,max(.085,roughness*.22),damp);
 normal=normalize(lerp(normal,geometryNormal,damp*.28));
 // Fine, filtered moving disturbance of the water film rather than enlarged texture pixels.
 float4 ripple=slFilteredSurfaceNoise(p+float3(time*.13,0,-time*.1),17,footprint);
 float3 slope=ripple.xyz*.002*wet;slope-=geometryNormal*dot(slope,geometryNormal);
 normal=normalize(normal-slope);
}
// GPU terrain packets use a separate range for continuous paint blends.
// Legacy packets and discrete object/grass tags retain their original meaning.
float slSurfaceKind(float kind){return kind>=-32&&kind<-25?kind+32:kind;}
bool slGrassBlade(float kind){return kind>1.3&&kind<1.4;}
SlSurfaceMaterial slGrassBladeMaterial(float3 leaf,float3 normal,float footprint){
 float u=saturate(leaf.x),height=saturate(leaf.y),variation=saturate(leaf.z);
 float root=smoothstep(0,.3,height),tip=smoothstep(.7,1,height);
 float rib=exp(-pow((u-.5)*12,2));
 float resolved=1-smoothstep(.001,.004,footprint);
 float veins=cos(u*56.5487+variation*3)*resolved;
 SlSurfaceMaterial m;m.normal=normal;
 m.tint=lerp(float3(.13,.23,.055),float3(.4,.68,.16),root);
 m.tint=lerp(m.tint,float3(.64,.73,.28),tip*.5);
 m.tint*=lerp(.82,1.18,variation)*(1+.055*veins+.06*rib);
 m.roughness=clamp(.43+.15*(1-rib)+.06*variation,.4,.72);m.ao=lerp(.5,1,root);
 return m;
}
float3 slGrassThinLight(float3 albedo,float3 normal,float3 view,float3 light){
 float back=saturate(-dot(normal,light)),forward=pow(saturate(dot(view,-light)),4);
 return albedo*(back*.16+forward*.035);
}
float3 slStoneDetailPosition(float3 p){return float3(dot(p,float3(.819648,0,.572867)),dot(p,float3(-.259470,.891568,.371199)),dot(p,float3(-.510754,-.452886,.730866)));}
float3 slStoneDetailGradient(float3 g){return float3(.819648,0,.572867)*g.x+float3(-.259470,.891568,.371199)*g.y+float3(-.510754,-.452886,.730866)*g.z;}
float3 slBoundStoneSlope(float3 slope,float3 normal,float limit){slope-=normal*dot(slope,normal);return slope*min(1,limit/max(.00001,length(slope)));}
SlSurfaceMaterial slSurfaceMaterial(float3 p,float3 normal,float kind,float footprint){
 SlSurfaceMaterial m;m.tint=1;m.normal=normal;m.roughness=.78;m.ao=1;
 float grass=1-smoothstep(.15,.85,abs(kind-1)),sand=1-smoothstep(.15,.85,abs(kind-3));
 float rock=max(smoothstep(3.1,4,kind)*(1-smoothstep(4.2,5,kind)),smoothstep(5.2,6,kind));
 float snow=1-smoothstep(.15,.85,abs(kind-5));
 float4 macro=slFilteredSurfaceNoise(p,1.25,footprint),grain=slFilteredSurfaceNoise(p,7,footprint),fine=slFilteredSurfaceNoise(p,38,footprint);
 float4 strata=slFilteredSurfaceNoise(float3(p.x*.35,p.y*2.8,p.z*.35),3,footprint*2.8);
 strata.xyz*=float3(.35,2.8,.35);
 float variation=.78+.28*macro.w+.18*grain.w;
 m.tint=variation*lerp(float3(.9,.87,.82),float3(1.08,1.05,.97),fine.w*.45+macro.w*.55);
 m.tint*=lerp(1,float3(.86,1.06,.8),grass*.4);
 m.tint*=lerp(1,.9+.16*strata.w,rock);
 m.tint*=lerp(1,1.04+.12*fine.w,snow);
 float amplitude=lerp(.026,.014,sand);amplitude=lerp(amplitude,.009,snow);
 float3 gradient=(grain.xyz*.7+fine.xyz*.16+strata.xyz*rock*.4)*amplitude;
 gradient-=normal*dot(gradient,normal);
 m.normal=normalize(normal-gradient);
 m.roughness=clamp(.68+.22*grain.w+.1*fine.w+grass*.08+sand*.04-rock*.14-snow*.1,.35,.97);
 float slice=kind<=5?clamp(kind-1,0,4):lerp(4,3,saturate(kind-5));
 float3 weights=pow(abs(normal),4);weights/=max(.00001,weights.x+weights.y+weights.z);
 float2 uv[3]={p.zy*2,p.xz*2,p.xy*2};float3 mapTint=0,mapORM=0,gradientMap=0;
 [unroll]for(int axis=0;axis<3;++axis){
  float weight=weights[axis];if(weight>.001){
   mapTint+=slSurfaceLayer(slSurfaceAlbedo,uv[axis],slice,footprint).rgb*weight;
   mapORM+=slSurfaceLayer(slSurfaceORM,uv[axis],slice,footprint).rgb*weight;
   float3 guide=slSurfaceLayer(slSurfaceNormal,uv[axis],slice,footprint).xyz*2-1;
   float2 slope=-guide.xy/max(.25,guide.z);
   gradientMap+=(axis==0?float3(0,slope.y,slope.x):axis==1?float3(slope.x,0,slope.y):float3(slope.x,slope.y,0))*weight;
  }
 }
 gradientMap-=normal*dot(gradientMap,normal);
 m.tint=pow(max(m.tint,0),2.2)*mapTint;m.normal=normalize(m.normal-gradientMap);
 m.roughness=clamp(mapORM.y,.15,.97);m.ao=mapORM.x;
 if(rock>.001){
  // Preserve the original generic calculation above, including zero-weight
  // terms for unrelated kinds. Rebuild stone tint/normal without y-strata.
  float3 rockTint=variation*lerp(float3(.9,.87,.82),float3(1.08,1.05,.97),fine.w*.45+macro.w*.55);
  rockTint*=lerp(1,float3(.86,1.06,.8),grass*.4);
  float rockPatch=slFilteredSurfaceNoise(slStoneDetailPosition(p)+float3(17,3,29),2.3,footprint).w;
  rockTint*=lerp(1,.93+.12*rockPatch,rock);
  rockTint*=lerp(1,1.04+.12*fine.w,snow);
  m.tint=pow(max(rockTint,0),2.2)*mapTint;
  float3 cleanGradient=(grain.xyz*.7+fine.xyz*.16)*amplitude;
  cleanGradient-=normal*dot(cleanGradient,normal);
  m.normal=normalize(normalize(normal-cleanGradient)-gradientMap);
  // Rotated, isotropic geological scales break the former y-strata and
  // tri-planar crosshatching. Map relief stays, but surface normals retain
  // the real slab/facet response with bounded small mineral slopes.
  float3 q=slStoneDetailPosition(p);
  float4 mineral=slFilteredSurfaceNoise(q+float3(13,7,23),5.3,footprint),grit=slFilteredSurfaceNoise(q+float3(31,11,5),31,footprint);
  float3 stoneSlope=slBoundStoneSlope(gradientMap*.65+slStoneDetailGradient(mineral.xyz*.0035+grit.xyz*.00065),normal,.30);
  m.normal=normalize(lerp(m.normal,normalize(normal-stoneSlope),rock));
  m.tint*=lerp(1,lerp(float3(.94,.96,.98),float3(1.06,1.03,.98),mineral.w)*(.94+.12*grit.w),rock);
  m.roughness=lerp(m.roughness,clamp(mapORM.y*.72+.22+.05*grit.w,.72,.96),rock);
 }
 // Fresh geological breaks use the same filtered rock maps with a lighter,
 // rougher mineral interior. Exterior stone and legacy material IDs retain
 // their authored response. The cap geometry supplies its own hard normal.
 if(kind>6.35&&kind<6.55){
  // Angular mineral cleavage and dark crystalline inclusions, rather than a
  // uniform bright porous face. Footprint filtering suppresses unresolved grain.
  float3 q=slStoneDetailPosition(p);
  float4 cleavage=slFilteredSurfaceNoise(q+float3(19,41,3),11,footprint),mineral=slFilteredSurfaceNoise(q+float3(7,17,37),47,footprint);
  float fleck=smoothstep(.64,.84,mineral.w);
  float3 gradient=slBoundStoneSlope(gradientMap*.35+slStoneDetailGradient(cleavage.xyz*.0035+mineral.xyz*.00055),normal,.22);
  m.tint*=lerp(float3(.88,.90,.92),float3(1.12,1.10,1.06),cleavage.w)*(1-.12*fleck);
  m.roughness=clamp(max(.84,m.roughness)+.025*fleck,.84,.97);m.normal=normalize(normal-gradient);
  m.ao=lerp(.90,.98,cleavage.w);
 }
 return m;
}
struct SlMagicStoneMaterial {float3 normal;float3 color;float3 emission;float roughness;float ao;};
SlMagicStoneMaterial slMagicStoneMaterial(float3 p,float3 normal,float kind,float3 authoredTint,float time,float footprint){
 SlSurfaceMaterial stone=slSurfaceMaterial(p,normal,6,footprint);
 float3 drift=p+float3(time*.045,-time*.035,time*.025);
 float3 warp=float3(slFilteredSurfaceNoise(p+float3(0,time*.035,0),.9,footprint).w,slFilteredSurfaceNoise(p+float3(11+time*.025,17,23),.9,footprint).w,slFilteredSurfaceNoise(p+float3(29,5,13-time*.02),.9,footprint).w)-.5;
 float3 magicPosition=drift+warp*.16;
 float4 broad=slFilteredSurfaceNoise(magicPosition,3.6,footprint*1.25),fine=slFilteredSurfaceNoise(magicPosition,10.8,footprint*1.25);
 float field=broad.w+.3*fine.w-.62;
 float width=max(.001,footprint*length(broad.xyz+.3*fine.xyz));
 float fissure=(1-smoothstep(.018,.032+width,abs(field)))*.032/(.032+width);
 float core=(1-smoothstep(.004,.011+width,abs(field)))*.011/(.011+width);
 float resolved=1-smoothstep(.12,.5,footprint*3.6);fissure=lerp(.065,fissure,resolved);core=lerp(.023,core,resolved);
 float pulse=.8+.2*sin(time*2+p.y*3),shard=step(10.5,kind);
 float3 groove=(broad.xyz+.3*fine.xyz)*sign(field)*smoothstep(.006,.018,abs(field))*(1-smoothstep(.025,.06,abs(field)))*.08;groove-=normal*dot(groove,normal);
 SlMagicStoneMaterial m;m.normal=normalize(lerp(stone.normal,normal,.8*shard)+groove*(1-shard*.7));
 m.color=lerp(float3(.13,.09,.16),float3(.09,.035,.15),shard)*stone.tint*authoredTint*(1-fissure*.7);
 m.roughness=lerp(stone.roughness,clamp(.15+.2*fine.w,.15,.4),shard);m.ao=lerp(stone.ao,1,shard);
 m.emission=(float3(.7,.008,1.6)*fissure*.18+float3(.85,.13,1.4)*core*2)*pulse;
 m.emission+=float3(.035,.008,.08)*shard*pulse;
 if(kind>10.15&&kind<10.35){m.color*=1.25;m.roughness=max(.84,m.roughness);}
 return m;
}
struct SlWaterMaterial {float3 normal;float3 color;float roughness;float foam;};
bool slWaterDroplet(float kind){return kind>7.1&&kind<7.3;}
// Beer-Lambert absorption shared by refraction and light crossing the water.
float3 slWaterTransmission(float distance){return exp(-max(distance,0)*float3(.35,.09,.045));}
// Continuously moving cells make linked bubble rims and refracted light nets.
// The two nearest distances stay continuous when their cell identities change.
float slWaterCellEdge(float2 p,float time){
 float2 cell=floor(p),fraction=frac(p);float first=100,second=100;
 [unroll]for(int z=-1;z<=1;++z)[unroll]for(int x=-1;x<=1;++x){
  float2 offset=float2(x,z),key=cell+offset;
  float2 random=float2(slSurfaceHash(float3(key,71)),slSurfaceHash(float3(key,113)));
  float2 center=offset+.5+.28*sin(random*6.283185+time*float2(.57,.43));
  float distance=length(center-fraction);
  if(distance<first){second=first;first=distance;}else second=min(second,distance);
 }
 return second-first;
}
float slWaterFilteredRim(float edge,float width,float footprint){
 float filter=max(.001,footprint*2);
 return (1-smoothstep(max(0,width-filter),width+filter,edge))*width/(width+filter);
}
float slWaterFocusedLine(float edge,float width,float footprint){
 // A finite soft focus conserves the line's energy as the pixel widens.
 float filter=max(.001,footprint*1.5),variance=width*width+filter*filter;
 return exp(-edge*edge/variance)*width*rsqrt(variance);
}
float slWaterCaustics(float2 p,float time,float footprint){
 float3 field=float3(p.x,0,p.y);
 float2 position=p*2.4+float2(time*.08,-time*.055);
 // Smooth crossed waves bend the complete focus field, including its edges.
 // The bounded warp has no hard cell-level offsets or time-randomized changes.
 float2 bend=float2(.42*sin(position.y*1.8+.3*sin(position.x*.8)+time*.63),.42*sin(position.x*1.6-.3*sin(position.y*.7)-time*.54));
 float edge=slWaterCellEdge(position+bend,time*.65);
 // Bound the warp Jacobian when choosing the footprint. A single curved net
 // has a soft halo and brighter focus core, rather than two overlaid polygons.
 float lensFootprint=footprint*2.4*2;
 float focus=slWaterFocusedLine(edge,.03,lensFootprint),halo=slWaterFocusedLine(edge,.09,lensFootprint);
 float brightness=slFilteredSurfaceNoise(field+float3(time*.11,5,-time*.085),1.45,footprint).w;
 float concentration=smoothstep(.26,.76,brightness);
 float variableFocus=lerp(.22,1,concentration);
 // Unresolved lenses vanish instead of turning into uniform extra sunlight.
 return (focus*.65+halo*.35)*variableFocus*(1-smoothstep(.18,.55,lensFootprint));
}
SlWaterMaterial slWaterDropMaterial(float3 normal){
 SlWaterMaterial m;m.normal=normal;m.color=float3(.006,.035,.05);m.roughness=.04;m.foam=0;return m;
}
SlWaterMaterial slWaterMaterial(float3 p,float3 normal,float time,float depth,float footprint){
 float2 slope=0;float crest=0;
 float2 direction[8]={float2(.94,.342),float2(-.6,.8),float2(.242,-.97),float2(-.857,-.514),float2(.79,-.613),float2(-.35,.937),float2(.996,.089),float2(-.719,-.695)};
 float frequency[8]={1.1,1.7,2.9,4.7,8.3,13.1,21.7,33.9};float amplitude[8]={.018,.012,.009,.006,.0035,.0018,.0009,.0004};
 [unroll]for(int wave=0;wave<8;wave++){
  float k=frequency[wave],phase=dot(direction[wave],p.xz)*k-time*sqrt(9.81*k);
  float attenuation=1-smoothstep(.4,1.5,footprint*k);
  slope+=direction[wave]*(amplitude[wave]*k*cos(phase)*attenuation);crest+=sin(phase)*amplitude[wave]*attenuation;
 }
 SlWaterMaterial m;float3 gradient=float3(slope.x,0,slope.y);gradient-=normal*dot(gradient,normal);m.normal=normalize(normal-gradient);
 float3 drift=p+float3(time*.095,0,-time*.065);
 float4 washNoise=slFilteredSurfaceNoise(drift,2.8,footprint),breakup=slFilteredSurfaceNoise(drift,11,footprint);
 // Bed-relative depth supplied by the moving mesh anchors the wash to shore.
 // A soft contact core and retreating broken ribbons avoid a flat noise band.
 float band=max(.012,footprint*.6),bed=max(depth,0);
#ifdef SL_SURFACE_PIXEL
 band=max(band,fwidth(depth)*.5);
#endif
 float shore=1-smoothstep(.035-band,.36+band,bed);
 float contact=1-smoothstep(.004-band,.035+band,bed);
 float wash=smoothstep(.43,.70,washNoise.w+crest*4);
 float coverage=wash*smoothstep(.30,.67,breakup.w+washNoise.w*.2);
 float ribbonPhase=bed*24-time*1.65+washNoise.w*4;
 float ribbonFilter=max(.03,band*24);
 float ribbon=(1-smoothstep(.16,.4+ribbonFilter,abs(sin(ribbonPhase))))*.4/(.4+ribbonFilter);
 float2 bubblePosition=drift.xz*18;
 bubblePosition+=.22*sin(bubblePosition.yx*1.7+float2(time*.7,-time*.6));
 float cells=slWaterFilteredRim(slWaterCellEdge(bubblePosition,time*.8),.09,footprint*18*1.45);
 cells=lerp(.14,cells,1-smoothstep(.15,.7,footprint*18*1.45));
 float bubbles=lerp(cells,.68,smoothstep(.57,.79,breakup.w));
 float breaking=smoothstep(.10,.30,1-saturate(normal.y))*(1-smoothstep(.3,1.2,depth));
 // Contact foam has no permanent white base: broken froth arrives with the
 // wash, then leaves thin bubbly ribbons and open transparent water between.
 m.foam=saturate(contact*coverage*(.22+.55*bubbles)+shore*ribbon*coverage*bubbles*.9+breaking*coverage*.35);
 float3 transmission=exp(-max(depth,0)*float3(.85,.22,.095));
 m.color=float3(.006,.035,.05)*(1-transmission)+float3(.12,.16,.105)*transmission;
 m.roughness=lerp(.065,.68,m.foam);m.color=lerp(m.color,float3(.58,.66,.64),m.foam);
 return m;
}
struct SlLavaMaterial {float3 normal;float3 color;float3 emission;float roughness;};
float slLavaCellHash(int2 cell,uint seed){
 uint v=uint(cell.x)*0x9e3779b9u^uint(cell.y)*0x85ebca6bu^seed;v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;return float(v)/4294967296.0;
}
float4 slLavaFlowNoise(float2 p,float frequency,float footprint,uint seed);
struct SlLavaCrustField {float cover;float2 gradient;float plate;float junction;};
SlLavaCrustField slLavaCrustField(float2 p,float time,float heat,float footprint,bool geometry){
 if(heat==0)time=0;
 float2 position=(p+float2(time*.018,-time*.025))*1.15+float2(.22*sin(p.y*.7+time*.09)+.10*sin(p.y*2.3+p.x*.9),.22*sin(p.x*.6-time*.07)+.10*sin(p.x*2.1-p.y*.8));
 int2 cell=int2(floor(position));float2 fraction=frac(position);float first=100,second=100,third=100,plate=.5;float2 firstGradient=0,secondGradient=0;
 [unroll]for(int z=-1;z<=1;++z)[unroll]for(int x=-1;x<=1;++x){int2 key=cell+int2(x,z);float2 center=float2(x,z)+.05+.9*float2(slLavaCellHash(key,17),slLavaCellHash(key,29));float2 delta=fraction-center;float d=length(delta);float2 gradient=delta/max(d,.0001);if(d<first){third=second;second=first;secondGradient=firstGradient;first=d;firstGradient=gradient;plate=slLavaCellHash(key,43);}else if(d<second){third=second;second=d;secondGradient=gradient;}else third=min(third,d);}
 float low=.012+.012*heat,high=.045+.075*heat,filter=geometry?0:max(.001,footprint*1.15);
 float cover=smoothstep(max(0,low-filter),high+filter,second-first);
 // Footprint filtering keeps narrow incandescent gaps stable in the distance.
 float resolved=1-smoothstep(.3,.8,footprint*1.15);
 float2 edgeGradient=secondGradient-firstGradient;
 // Analytic derivative of the same warped cell field used by actual crust geometry.
 float2 dx=float2(1.15+.09*cos(p.y*2.3+p.x*.9),.132*cos(p.x*.6-time*.07)+.21*cos(p.x*2.1-p.y*.8));
 float2 dz=float2(.154*cos(p.y*.7+time*.09)+.23*cos(p.y*2.3+p.x*.9),1.15-.08*cos(p.x*2.1-p.y*.8));
 float span=high+filter-max(0,low-filter),u=saturate((second-first-max(0,low-filter))/span);
 float2 cellGradient=float2(dot(edgeGradient,dx),dot(edgeGradient,dz))*(6*u*(1-u)/span);
 // Exact CPU lava_crust coarse welding/fracture field: fewer closed tile
 // outlines, larger irregular raised masses and genuinely recessed cracks.
 float4 mass=slLavaFlowNoise(p+float2(time*.018,-time*.025),.65,footprint,131);
 float weldU=saturate((mass.w-.22)/.5),weld=.8*weldU*weldU*(3-2*weldU);
 float2 weldGradient=mass.xz*(.8*6*weldU*(1-weldU)/.5);
 float fractureU=saturate((abs(mass.w-.49)-.008)/.042);
 float fracture=1-fractureU*fractureU*(3-2*fractureU);
 float massResolved=1-smoothstep(.25,1.2,footprint*.65);
 float2 fractureGradient=-sign(mass.w-.49)*mass.xz*(6*fractureU*(1-fractureU)/.042)*massResolved;
 fracture=lerp(.06,fracture,massResolved);weld=lerp(.45,weld,massResolved);weldGradient*=massResolved;
 float joined=cover+(1-cover)*weld;
 float2 joinedGradient=cellGradient*(1-weld)+(1-cover)*weldGradient;
 SlLavaCrustField field;field.cover=saturate(lerp(.85,joined*(1-fracture*.9),resolved));
 field.gradient=(joinedGradient*(1-fracture*.9)-joined*.9*fractureGradient)*resolved;
 field.plate=lerp(.5,plate,resolved);
 field.junction=lerp(.12,1-smoothstep(.08,.28,third-first),resolved);return field;
}
SlLavaCrustField slLavaCrustField(float2 p,float time,float heat,float footprint){return slLavaCrustField(p,time,heat,footprint,false);}
float4 slLavaFlowNoise(float2 p,float frequency,float footprint,uint seed){
 float attenuation=1-smoothstep(.35,1.2,footprint*frequency);
 p*=frequency;int2 cell=int2(floor(p));float2 f=frac(p),u=f*f*(3-2*f),du=6*f*(1-f);
 float a=slLavaCellHash(cell,seed),b=slLavaCellHash(cell+int2(1,0),seed),c=slLavaCellHash(cell+int2(0,1),seed),d=slLavaCellHash(cell+1,seed);
 float nearValue=lerp(a,b,u.x),farValue=lerp(c,d,u.x);
 float2 gradient=float2(lerp(b-a,d-c,u.y)*du.x,(farValue-nearValue)*du.y)*frequency*attenuation;
 return float4(gradient.x,0,gradient.y,lerp(.5,lerp(nearValue,farValue,u.y),attenuation));
}
struct SlLavaPassageSample {float4 islands;float4 folds;float4 streams;float3 streamGradient;float3 passageGradient;float passage;float heatOpen;float exposed;};
SlLavaPassageSample slLavaPassage(float2 p,float time,float heat,float footprint){
 if(heat==0)time=0;float2 flow=p+float2(time*.018,-time*.025);
 SlLavaPassageSample field;
 field.islands=slLavaFlowNoise(flow,1.6,footprint,83);
 field.folds=slLavaFlowNoise(flow*float2(.7,1.6)+float2(time*.008,0),4,footprint*1.6,59);
 field.streams=slLavaFlowNoise(flow*float2(.38,.73)+float2(time*.009,0),2,footprint*.73,97);
 field.streamGradient=field.streams.xyz*float3(.38,0,.73);
 float streamValue=field.streams.w-.48,streamWidth=.08+.08*field.islands.w;
 float streamFilter=max(.008,footprint*length(field.streamGradient));
 float streamLow=max(0,.025-streamFilter),streamHigh=streamWidth+streamFilter;
 float streamU=saturate((abs(streamValue)-streamLow)/max(.001,streamHigh-streamLow));
 float passage=1-streamU*streamU*(3-2*streamU);
 float streamResolved=1-smoothstep(.25,1.2,footprint*1.8);
 field.passage=saturate(lerp(.38,passage,streamResolved));
 field.passageGradient=-sign(streamValue)*field.streamGradient*(6*streamU*(1-streamU)/max(.001,streamHigh-streamLow))*streamResolved;
 float raftBreakup=smoothstep(.57,.78,field.islands.w+.14*field.folds.w);
 field.heatOpen=smoothstep(.18,.80,heat);
 field.exposed=saturate(field.heatOpen*(1-(1-field.passage)*(1-raftBreakup*.82)));return field;
}
float slLavaCrust(float2 p,float time,float heat,float footprint){return saturate(slLavaCrustField(p,time,heat,footprint).cover*(1-slLavaPassage(p,time,heat,footprint).exposed));}
// Unfiltered geometry contract, paired with CPU lava_crust. Pixel materials
// retain their intentional minimum .001 edge filter through the wrapper above.
float2 slLavaGeometry(float2 p,float time,float heat,float depth,float thickness){
 heat=saturate(heat);if(heat==0)time=0;
 SlLavaCrustField field=slLavaCrustField(p,time,heat,0,true);
 float cover=saturate(field.cover*(1-slLavaPassage(p,time,heat,0).exposed));
 float2 flow=p+float2(time*.018,-time*.025);
 float mass=slLavaFlowNoise(flow,.65,0,131).w,rib=slLavaFlowNoise(flow,5.2,0,137).w;
 float heightShape=.88+.08*mass+.04*rib;
 float heightCover=cover/(.18+.82*cover);
 float relief=heightCover*min(max(0,thickness),max(0,depth)*.3)*(.90+.10*(1-heat))*(.94+.06*field.plate)*heightShape;
 return float2(cover,max(0,relief));
}
SlLavaMaterial slLavaMaterial(float3 p,float3 normal,float time,float footprint,float kind,float3 surface){
 if(kind>8.05&&kind<8.15){
  float heat=saturate(surface.y),clock=heat==0?0:surface.x;
  SlLavaCrustField field=slLavaCrustField(p.xz,clock,heat,footprint);
  // Texture moves with the cell drift; cold authored lava freezes all detail too.
  float3 flow=p+float3(clock*.018,0,-clock*.025);
  SlSurfaceMaterial basalt=slSurfaceMaterial(flow,normal,4,footprint);
  SlLavaPassageSample thermal=slLavaPassage(p.xz,clock,heat,footprint);
  float4 folds=thermal.folds,islands=thermal.islands,streams=thermal.streams;
  float4 grain=slFilteredSurfaceNoise(flow,18,footprint),pores=slFilteredSurfaceNoise(flow,47,footprint);
  // The molten phase has connected, metre-scale meandering passages rather
  // than illuminating every cell edge equally. Cooling closes these passages
  // into the original cold crust field. CPU lava_passage uses the identical
  // integer noise, seeds and thresholds to lower those actual mesh regions.
  float3 streamGradient=thermal.streamGradient,passageGradient=thermal.passageGradient;
  float passage=thermal.passage,heatOpen=thermal.heatOpen,exposed=thermal.exposed;
  float crust=saturate(field.cover*(1-exposed)),melt=1-crust;
  // Nonperiodic, stretched wrinkles follow the drift. Soft cooling patches
  // vary radiance moderately instead of drawing black sine-wave contours.
  float4 foldBreak=slLavaFlowNoise(flow.xz+float2(clock*.024,-clock*.011),7.3,footprint,149);
  float4 wrinkles=slLavaFlowNoise(flow.xz*float2(.48,1.85)+float2(clock*.014,0),6.8,footprint*1.85,151);
  float ropes=wrinkles.w;
  float3 ropeGradient=wrinkles.xyz*float3(.48,0,1.85);
  float coolingSkin=saturate(.10+.30*foldBreak.w+.20*folds.w);
  float poreBand=max(.035,footprint*47*.18);
  float vesicles=smoothstep(.60-poreBand,.76+poreBand,pores.w);
  float ridge=abs(grain.w-.48),ridgeSign=grain.w>=.48?1:-1;
  float ridgeFilter=1-smoothstep(.25,1,footprint*18);
  float3 relief=islands.xyz*.025+grain.xyz*(ridgeSign*.005*ridgeFilter)+pores.xyz*(.0011*crust);
  // Existing chipped cell lips remain on surviving raised rafts. The broad
  // thermal front adds fine shading relief on the matching CPU passage banks.
  relief+=float3(field.gradient.x,0,field.gradient.y)*.003;
  relief-=passageGradient*heatOpen*.012;
  float3 moltenSlope=folds.xyz*float3(.7,1,1.6)*.008+ropeGradient*.003;
  relief=relief*crust+moltenSlope*melt*heat;relief-=normal*dot(relief,normal);
  SlLavaMaterial m;m.normal=normalize(lerp(normal,basalt.normal,crust*.38)-relief);
  float stoneVariation=(.66+.48*field.plate)*(.70+.60*grain.w)*(1-.38*vesicles);
  float ash=smoothstep(.25,.79,islands.w+.15*grain.w);
  float3 stoneColor=lerp(float3(.050,.048,.047),float3(.095,.092,.090),ash);
  float3 skinColor=lerp(float3(.012,.009,.008),float3(.045,.036,.031),coolingSkin);
  m.color=lerp(skinColor,basalt.tint*stoneColor*stoneVariation,crust);
  // Most remote cell seams are faint red embers, not white outlines. Broad
  // exposed passages carry the hot radiance; surviving closed interiors stay dark.
  float cellShoulder=saturate(1-pow(field.cover,2+islands.w*2));
  cellShoulder=saturate(lerp(.23,cellShoulder,1-smoothstep(.3,.8,footprint*1.15)));
  float seamExposure=cellShoulder*(.035+.30*passage)*(.5+.5*folds.w);
  float emissionArea=saturate(exposed+(1-exposed)*seamExposure);
  float hotPocket=saturate(.15+.72*folds.w+.24*ropes+.28*field.junction);
  float temperature=saturate(heat*(.42+.58*hotPocket)*(.55+.45*exposed));
  float radiance=pow(heat,4),hotCore=pow(saturate(temperature),3);
  float3 glow=lerp(float3(.30,.003,.0005),float3(2.6,.32,.008),hotCore);
  glow=lerp(glow,float3(4.3,.95,.055),smoothstep(.90,1,temperature)*.28);
  float moltenDetail=(.46+.54*hotPocket)*(.72+.28*(1-coolingSkin));
  m.emission=max(0,glow*radiance*emissionArea*moltenDetail);
  float crustRoughness=clamp(.85+.09*vesicles+.07*ridge+.025*field.plate,.84,.98);
  float moltenRoughness=lerp(.22+.34*coolingSkin,.90,pow(1-heat,2));
  m.roughness=lerp(moltenRoughness,crustRoughness,crust);
  // Frozen fractures are rough recessed basalt, not blue glossy liquid seams.
  m.color=lerp(m.color,basalt.tint*float3(.025,.025,.026),(1-crust)*pow(1-heat,2));return m;
 }
 float3 flow=p+float3(time*.012,0,-time*.017);
 float4 macro=slFilteredSurfaceNoise(flow,3,footprint),detail=slFilteredSurfaceNoise(flow,8.1,footprint);
 float2 warp=float2(slFilteredSurfaceNoise(flow,1.3,footprint).w,slFilteredSurfaceNoise(flow+13,1.7,footprint).w);
 float2 position=flow.xz*1.3+warp*.85,cell=floor(position),fraction=frac(position);
 float nearest=100,second=100,plate=0;
 [unroll]for(int z=-1;z<=1;++z)[unroll]for(int x=-1;x<=1;++x){
  float2 offset=float2(x,z),key=cell+offset;
  float2 center=offset+float2(slSurfaceHash(float3(key,17)),slSurfaceHash(float3(key,29)))*.7+.15;
  float distance=length(center-fraction);
  if(distance<nearest){second=nearest;nearest=distance;plate=slSurfaceHash(float3(key,43));}else second=min(second,distance);
 }
 float edge=second-nearest,width=max(.009,footprint*1.3);
 float crack=1-smoothstep(max(.001,.035-width),.12+width,edge);
 float broken=smoothstep(.77,.94,plate)*(.45+.3*detail.w);
 float melt=saturate(crack+broken);
 float heat=slFilteredSurfaceNoise(flow,6,footprint).w;
 SlLavaMaterial m;float3 gradient=(macro.xyz+.24*detail.xyz)*.035*(1-melt);gradient-=normal*dot(gradient,normal);m.normal=normalize(normal-gradient);
 m.color=lerp(float3(.009,.006,.006),float3(.035,.027,.023),detail.w)*(.65+.45*plate);
 float hotCore=pow(saturate(crack),3);
 m.emission=lerp(float3(2.7,.11,.002),float3(7.5,2.1,.13),hotCore*(.55+.45*heat))*melt*(.92+.08*sin(time*.8+p.x*.4+p.z*.3));
 m.roughness=lerp(.94,.38,melt);return m;
}
// Closed channels use an angle-dependent core, independent of ribbon UVs.
// Keep the legacy ribbon branch for older stored mesh packets.

float3 slLightningEmission(float kind,float3 surface,float3 normal,float3 view,float time){
 if(kind>9.3&&kind<9.4){
  float facing=saturate(abs(dot(normalize(normal),normalize(view))));
  float core=pow(facing,5),age=frac(max(0,time)/.22)*.22;
  float flash=(.48+.52*exp(-age*18))*(.94+.06*sin(time*83));
  float3 violet=lerp(float3(.65,.018,2.8),float3(1.3,.025,3.6),surface.x);
  return (violet*(1+2*facing)+float3(22,17,28)*core)*flash*lerp(1,.7,surface.y);
 }
 float edge=abs(surface.x*2-1);
 return (lerp(float3(1,.025,.28),float3(.22,.055,1),surface.y)*(2+5*(1-edge))+pow(saturate(1-edge),6)*float3(18,16,20))*(.72+.28*sin(time*37+floor(time*19)*3.1));
}
#endif
