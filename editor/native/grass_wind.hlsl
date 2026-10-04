bool gpuGrassSurface(float kind){return kind>1.355&&kind<1.365;}
float3 gpuGrassWind(float3 p,float time){
 float3 world=mul(float4(p,1),model).xyz;
 float bend=saturate(p.y/.6);bend*=bend;
 // Steady-state critically damped wind response, evaluated per vertex on the
 // GPU. Root vertices remain fixed. Keep the authored mesh resident.
 float omega=12;
 float a=omega*omega/(omega*omega+1.7*1.7),b=omega*omega/(omega*omega+4.1*4.1),c=omega*omega/(omega*omega+1.3*1.3);
 float fade=1-exp(-omega*max(0,time))*(1+omega*max(0,time));
 float3 offset=bend*fade*float3(.10*a*sin(time*1.7+world.x*.4+world.z*.25-2*atan(1.7/omega))+.025*b*sin(time*4.1+world.z-2*atan(4.1/omega)),0,.07*c*cos(time*1.3+world.z*.3-2*atan(1.3/omega)));
 float3 scale2=float3(dot(model[0].xyz,model[0].xyz),dot(model[1].xyz,model[1].xyz),dot(model[2].xyz,model[2].xyz));
 return p+float3(dot(offset,model[0].xyz),dot(offset,model[1].xyz),dot(offset,model[2].xyz))/max(scale2,.000001);
}
float3 gpuGrassNormal(float3 p,float3 normal,float time){
 // Transform the normal with the deformation Jacobian, so bending changes
 // leaf lighting as well as position. No CPU normal reconstruction is needed.
 const float e=.0005;
 float3 x=mul(float4((gpuGrassWind(p+float3(e,0,0),time)-gpuGrassWind(p-float3(e,0,0),time))/(2*e),0),model).xyz;
 float3 y=mul(float4((gpuGrassWind(p+float3(0,e,0),time)-gpuGrassWind(p-float3(0,e,0),time))/(2*e),0),model).xyz;
 float3 z=mul(float4((gpuGrassWind(p+float3(0,0,e),time)-gpuGrassWind(p-float3(0,0,e),time))/(2*e),0),model).xyz;
 return normalize(normal.x*cross(y,z)+normal.y*cross(z,x)+normal.z*cross(x,y));
}
float3 environmentPosition(float3 p,float4 surface,float time){
 if(gpuGrassSurface(surface.w))return gpuGrassWind(p,time);
 float3 world=mul(float4(p,1),model).xyz;
 if(surface.w>.5&&surface.w<1.2){float bend=max(p.y,0);bend*=bend;float gust=sin(time*1.7+world.x*.4+world.z*.25);p.x+=bend*(.12*gust+.035*sin(time*4.1+world.z));p.z+=bend*(.075*cos(time*1.3+world.z*.3)+.02*sin(time*3+world.x));}
 if(params.y<2&&surface.w>8.5&&surface.w<9.2){float anchor=surface.y;p.x+=anchor*(sin(p.y*13+time*29)*.12+sin(p.y*29-time*41)*.045);p.z+=anchor*(cos(p.y*17-time*31)*.12+sin(p.y*31+time*37)*.045);}
 return p;
}
