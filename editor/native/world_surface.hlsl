float hash31(float3 p) { p=frac(p*.1031);p+=dot(p,p.yzx+33.33);return frac((p.x+p.y)*p.z); }
float noise3(float3 p) { float3 a=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(lerp(hash31(a),hash31(a+float3(1,0,0)),f.x),lerp(hash31(a+float3(0,1,0)),hash31(a+float3(1,1,0)),f.x),f.y),lerp(lerp(hash31(a+float3(0,0,1)),hash31(a+float3(1,0,1)),f.x),lerp(hash31(a+float3(0,1,1)),hash31(a+float3(1,1,1)),f.x),f.y),f.z); }
float detailNoise(float3 p) {float footprint=max(length(ddx(p)),length(ddy(p)));return lerp(noise3(p),.5,saturate(footprint-.4));}
float relief(float3 p,float k){return detailNoise(p*5)*.58+detailNoise(p*18)*.28+detailNoise(p*65)*.14;}
PSOut PSMain(VSOut i) {
 PSOut po;float2 cn=i.cur.xy/i.cur.w,pn=i.prv.xy/i.prv.w;po.mvec=(pn-cn)*mvScale.xy;
 float3 n=normalize(i.wn);if(dot(n,camPos.xyz-i.wp)<0)n=-n;
 float kind=i.surface.w,time=params.w;float3 color=baseColor.rgb*i.surface.rgb;
 float rough=.78,emission=baseColor.a;float3 wp=i.wp;
 if(kind>.5&&kind<6.8){
  float grain=relief(wp,kind);float coarse=detailNoise(wp*1.3);
  color*=.68+.55*grain+.18*coarse;
  float grass=1-smoothstep(.15,.85,abs(kind-1));
  float blades=detailNoise(float3(wp.x*110,wp.y*9,wp.z*27));color*=1+grass*(blades-.5)*.65;
  float stone=smoothstep(3,4,kind);float veins=detailNoise(wp*7.3);color*=lerp(1,.94+.12*veins,stone);
  float h=grain*.022;float3 dp1=ddx(wp),dp2=ddy(wp);float3 r1=cross(dp2,n),r2=cross(n,dp1);float det=dot(dp1,r1);float3 grad=(r1*ddx(h)+r2*ddy(h))/max(abs(det),.00001)*sign(det);n=normalize(n-grad);
 }
 float3 v=normalize(camPos.xyz-wp),l=-normalize(lightDir.xyz);
 float3 result;
 if(kind>6.8&&kind<7.5){
  float2 p=wp.xz;float wave=sin(p.x*4+time*1.2)+sin(p.y*5-time*.9)+.35*sin((p.x+p.y)*11+time*1.8);
  n=normalize(n+float3(cos(p.x*4+time*1.2)*.22+cos(p.y*13+time*2)*.04,0,cos(p.y*5-time*.9)*.18));
  float fresnel=.025+.975*pow(1-saturate(dot(n,v)),5);float3 sky=lerp(float3(.07,.22,.32),float3(.55,.78,.88),saturate(reflect(-v,n).y*.5+.5));
  float sparkle=pow(saturate(dot(reflect(-l,n),v)),160)*lightDir.w;
  float depth=i.surface.x*2;float shallow=exp(-depth*2.8);float3 bed=lerp(float3(.006,.06,.12),float3(.045,.24,.19),shallow);bed*=1+wave*.09+detailNoise(float3(p*8,time*.2))*.13;
  result=lerp(bed,sky,fresnel*.85+.08)+sparkle*.7;float foam=(1-smoothstep(.018,.09,depth))*smoothstep(.1,.9,noise3(float3(p*17,time*.4)));result=lerp(result,float3(.55,.7,.67),foam*.7);
  po.color=float4(result,1);return po;
 }
 if(kind>8.5&&kind<9.5){
  float edge=abs(i.surface.x*2-1);float core=pow(saturate(1-edge),6);float flicker=.72+.28*sin(time*37+floor(time*19)*3.1);
  result=lerp(float3(1,.025,.28),float3(.22,.055,1),noise3(wp*2+time))*(2+5*(1-edge))+core*float3(18,16,20);
  po.color=float4(result*flicker,1);return po;
 }
 if(kind>9.5){
  float f=noise3(wp*3.6)+.3*noise3(wp*10.8);float vein=1-smoothstep(.025,.07,abs(f-.62));float pulse=.8+.2*sin(time*2+wp.y*3);
  float3 rock=kind>10.5?float3(.11,.07,.18):float3(.018,.015,.022);
  result=shadeLin(wp,n,rock,.92,0,1,0)+float3(2.6,.045,4.8)*vein*pulse;
  if(kind>10.5)result+=float3(.25,.07,.55)*pulse;
  po.color=float4(result,1);return po;
 }
 if(kind>7.5){
  float3 p=wp*3+float3(time*.035,0,-time*.05);float field=noise3(p)+.24*noise3(p*2.7);float cracks=1-smoothstep(.028,.085,abs(field-.61));
  float pulse=.85+.15*sin(time*1.2+wp.x+wp.z);float3 crust=float3(.038,.013,.009)*(.7+noise3(p*8));float3 hot=lerp(float3(2.8,.15,.006),float3(4,1.25,.035),noise3(p*2));
  result=lerp(crust,hot*pulse,cracks);po.color=float4(result,1);return po;
 }
 result=shadeLin(wp,n,toLin(color),rough,0,1,emission);
 float visibility=1;
 [unroll]for(int shadow=0;shadow<16;shadow++){float4 bound=shadowBounds[shadow];float3 delta=bound.xyz-wp;float along=dot(delta,l);float side=length(delta-l*along);float outside=step(bound.w*1.035,length(delta));float shade=(1-smoothstep(bound.w*.6,bound.w*1.55,side))*step(.025,along)*outside*step(.01,bound.w);visibility*=1-shade*.65;}
 result*=lerp(visibility,1,emission);
 // Soft environment fill complements the directional key without flattening the normal detail.
 result+=toLin(color)*ambient.rgb*.13*saturate(n.y*.5+.5)*(1-emission);
 po.color=float4(finish(wp,result),1);return po;
}
