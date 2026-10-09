// Validate actual GPU readback, including the damped trajectory, rather than
// accepting two different screenshots as proof of correct interaction.
static json grass_gpu_checks(void* context,const std::vector<unsigned char>& packet){
 auto require=[](bool condition,const char* why){if(!condition)throw std::runtime_error(why);};
 auto frame=[&](double time){require(sl_effect_clock(context,time)&&sl_frame(context,time,13),sl_error());};
 auto read=[&]{int count=sl_grass_readback(context,0,nullptr,0);require(count>0,"No resident grass instance");std::vector<float> values(size_t(count)*14);require(sl_grass_readback(context,0,values.data(),count)==count,sl_error());return values;};
 std::array<double,16> matrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};wchar_t setting[512]{};
 if(GetEnvironmentVariableW(L"STARLIGHT_GRASS_TEST_WORLD",setting,512)){std::wistringstream input(setting);for(auto& value:matrix)require(bool(input>>value),"Invalid grass test transform");}
 auto worldPoint=[&](const float* p){std::array<double,3> w{};for(int j=0;j<3;++j)w[j]=p[0]*matrix[j]+p[1]*matrix[4+j]+p[2]*matrix[8+j]+matrix[12+j];return w;};
 auto worldOffset=[&](const float* p,const float* rest){std::array<double,3> w{};for(int j=0;j<3;++j)w[j]=(p[0]-rest[0])*matrix[j]+(p[1]-rest[1])*matrix[4+j]+(p[2]-rest[2])*matrix[8+j];return w;};
 sl_environment_reset(context);require(sl_grass_contacts(context,nullptr,0),sl_error());frame(0);auto rest=read();size_t count=rest.size()/14;
 float lowX=1e20f,lowZ=1e20f,highX=-1e20f,highZ=-1e20f;
 for(size_t i=0;i<count;++i){auto w=worldPoint(rest.data()+i*14);lowX=std::min(lowX,float(w[0]));highX=std::max(highX,float(w[0]));lowZ=std::min(lowZ,float(w[2]));highZ=std::max(highZ,float(w[2]));}
 std::array<float,6> body{lowX-.12f,-10,lowZ-.12f,highX+.18f,10,highZ+.18f};body[1]=float(matrix[13])-1;body[4]=float(matrix[13])+5;
 require(sl_grass_contacts(context,body.data(),1),sl_error());
 // This dedicated one-instance v4 fixture carries its exact triangle list.
 uint32_t format=0;memcpy(&format,packet.data(),4);require(format==0x34564c53,"Grass oracle requires the dedicated v4 fixture");size_t indexOffset=124+count*40;require(indexOffset+4<=packet.size(),"Grass fixture indices missing");uint32_t indexCount=0;memcpy(&indexCount,packet.data()+indexOffset,4);require(indexOffset+4+size_t(indexCount)*4==packet.size()&&indexCount%3==0,"Grass fixture topology mismatch");std::vector<uint32_t> indices(indexCount);memcpy(indices.data(),packet.data()+indexOffset+4,size_t(indexCount)*4);
 std::vector<std::array<double,3>> displacement(count),velocity(count);auto before=rest;double peakError=0,peakRoot=0,peakNormal=0,peakHistory=0,contactPeak=0,peakNormalReference=0;
 const float originalLow=body[0],originalHigh=body[3];
 double time=0;for(int step=0;step<120;++step){double dt=step%3==0?1./120:step%3==1?1./60:1./90;time+=dt;if(step<60){float shift=.1f*sinf(float(step)*.08f);body[0]=originalLow+shift;body[3]=originalHigh+shift;require(sl_grass_contacts(context,body.data(),1),sl_error());}if(step==60)require(sl_grass_contacts(context,nullptr,0),sl_error());frame(time);auto actual=read();
  for(size_t i=0;i<count;++i){const float* source=rest.data()+i*14;const float* got=actual.data()+i*14;auto w=worldPoint(source);double bend=std::clamp(double(source[1])/.6,0.,1.);bend*=bend;
   std::array<double,3> target{bend*(.10*sin(time*1.7+w[0]*.4+w[2]*.25)+.025*sin(time*4.1+w[2])),0,bend*.07*cos(time*1.3+w[2]*.3)};
   if(step<60&&w[1]>=body[1]-.1&&w[1]<=body[4]+.1&&w[0]>=body[0]-.2&&w[0]<=body[3]+.2&&w[2]>=body[2]-.2&&w[2]<=body[5]+.2){double distances[]{w[0]-body[0]+.2,body[3]+.2-w[0],w[2]-body[2]+.2,body[5]+.2-w[2]};int face=int(std::min_element(distances,distances+4)-distances);target[face<2?0:2]+=(face%2?1:-1)*std::min(.8,distances[face])*bend;contactPeak=std::max(contactPeak,std::abs(std::min(.8,distances[face])*bend));}
   auto offset=worldOffset(got,source);for(int j=0;j<3;++j){double error=displacement[i][j]-target[j],carry=velocity[i][j]+error*12,decay=exp(-12*dt);displacement[i][j]=target[j]+(error+carry*dt)*decay;velocity[i][j]=(velocity[i][j]-carry*12*dt)*decay;peakError=std::max(peakError,std::abs(displacement[i][j]-offset[j]));peakHistory=std::max(peakHistory,std::abs(double(got[10+j])-before[i*14+j]));if(!bend)peakRoot=std::max(peakRoot,std::abs(double(got[j])-source[j]));}
   double length=sqrt(got[3]*got[3]+got[4]*got[4]+got[5]*got[5]);peakNormal=std::max(peakNormal,std::abs(length-1));for(int j=0;j<14;++j)require(std::isfinite(got[j]),"Nonfinite GPU grass vertex");require(got[9]>1.365&&got[9]<1.375,"Grass compute tag did not prevent double wind");
  }
  if(step==0||step==59||step==119){std::vector<std::array<double,3>> normals(count);for(size_t t=0;t<indices.size();t+=3){for(int k=0;k<3;++k)require(indices[t+k]<count,"Grass fixture index out of range");const float* a=actual.data()+indices[t]*14;const float* b=actual.data()+indices[t+1]*14;const float* c=actual.data()+indices[t+2]*14;double x=b[0]-a[0],y=b[1]-a[1],z=b[2]-a[2],u=c[0]-a[0],v=c[1]-a[1],w=c[2]-a[2];std::array<double,3> n{y*w-z*v,z*u-x*w,x*v-y*u};for(int k=0;k<3;++k)for(int j=0;j<3;++j)normals[indices[t+k]][j]+=n[j];}for(size_t i=0;i<count;++i){const auto& n=normals[i];double length=sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);require(length>1e-10,"Grass deformed triangle normal collapsed");for(int j=0;j<3;++j)peakNormalReference=std::max(peakNormalReference,std::abs(n[j]/length-actual[i*14+3+j]));}}
  before=std::move(actual);
 }
 require(peakError<.00015,"GPU grass trajectory differs from critically damped CPU behavior");require(contactPeak>.1,"Grass fixture did not exercise contact bending");require(peakRoot<1e-7,"GPU grass moved its roots");require(peakNormal<.00002,"GPU grass normals are not unit length");require(peakHistory<.00001,"GPU grass previous positions do not match its preceding frame");require(peakNormalReference<.00002,"GPU grass normals do not follow actual triangle geometry");double motion[3]{};require(sl_motion_readback(context,motion)&&motion[0]>1e-6&&motion[2]>0,"GPU grass did not write rendered motion vectors");
 frame(time);auto same=read();for(size_t i=0;i<count;++i){for(int j=0;j<10;++j)require(same[i*14+j]==before[i*14+j],"Paused clock advanced GPU grass geometry");for(int j=0;j<3;++j)require(same[i*14+10+j]==same[i*14+j],"Paused grass retained stale motion history");}
 sl_environment_reset(context);frame(0);require(read()==rest,"Environment reset did not restore resident rest geometry");
 uint64_t stats[4]{};require(sl_grass_stats(context,stats)&&stats[0]>0&&stats[2]>=120,"Grass state/dispatch evidence unavailable");
 std::array<float,6> bad=body;bad[0]=std::numeric_limits<float>::quiet_NaN();require(!sl_grass_contacts(context,bad.data(),1),"Nonfinite grass contact accepted");bad=body;std::swap(bad[0],bad[3]);require(!sl_grass_contacts(context,bad.data(),1),"Inverted grass contact accepted");
 // Contacts outside the atmosphere grid and beyond its 128-body limit must
 // still reach grass. Harmless distant boxes precede the real contact.
 std::vector<float> many(130*6);for(int i=0;i<129;++i){float* box=many.data()+i*6;box[0]=box[1]=box[2]=1000+i;box[3]=box[4]=box[5]=1001+i;}std::copy(body.begin(),body.end(),many.begin()+129*6);
 require(sl_grass_contacts(context,many.data(),130),sl_error());frame(.2);auto late=read();sl_environment_reset(context);require(sl_grass_contacts(context,nullptr,0),sl_error());frame(0);frame(.2);auto noLate=read();double latePeak=0;for(size_t i=0;i<count;++i){auto offset=worldOffset(late.data()+i*14,noLate.data()+i*14);latePeak=std::max(latePeak,std::hypot(offset[0],offset[2]));}require(latePeak>.08,"Contact beyond fog obstacle capacity did not bend grass");
 uint32_t empty[]{0x31564c53,0};require(sl_scene(context,reinterpret_cast<unsigned char*>(empty),sizeof(empty)),sl_error());require(sl_grass_stats(context,stats)&&stats[0]==0,"Removed grass retained per-instance buffers");
 require(sl_scene(context,packet.data(),int(packet.size())),sl_error());require(sl_grass_contacts(context,nullptr,0),sl_error());sl_environment_reset(context);frame(0);require(read()==rest,"Grass unload/reload changed its rest geometry");
 // Two placements share identical topology but must not share spring state.
 auto pairPacket=packet;uint32_t two=2;memcpy(pairPacket.data()+4,&two,4);size_t second=pairPacket.size();pairPacket.insert(pairPacket.end(),packet.begin()+12,packet.end());float shiftedX=float(matrix[12]+20);memcpy(pairPacket.data()+second+44+48,&shiftedX,4);
 require(sl_scene(context,pairPacket.data(),int(pairPacket.size())),sl_error());sl_environment_reset(context);frame(0);require(sl_grass_contacts(context,body.data(),1),sl_error());frame(.2);require(sl_grass_stats(context,stats)&&stats[0]==2,"Shared grass placements did not retain separate instances");
 std::vector<float> other(count*14);require(sl_grass_readback(context,1,other.data(),int(count))==int(count),sl_error());double isolationError=0;double response=1-(1+12*.2)*exp(-12*.2);
 for(size_t i=0;i<count;++i){const float* source=rest.data()+i*14;auto w=worldPoint(source);w[0]+=20;double bend=std::clamp(double(source[1])/.6,0.,1.);bend*=bend;std::array<double,3> expected{response*bend*(.10*sin(.2*1.7+w[0]*.4+w[2]*.25)+.025*sin(.2*4.1+w[2])),0,response*bend*.07*cos(.2*1.3+w[2]*.3)};auto offset=worldOffset(other.data()+i*14,source);for(int j=0;j<3;++j)isolationError=std::max(isolationError,std::abs(offset[j]-expected[j]));}require(isolationError<.00015,"One grass placement leaked contact state into another");
 require(sl_scene(context,packet.data(),int(packet.size())),sl_error());require(sl_grass_contacts(context,nullptr,0),sl_error());sl_environment_reset(context);frame(0);
 // Shadow reach: held while the blade spring settles after any change in contact count, then relaxed to what the current contacts need, never below it.
 {
  auto reachNow=[&]{float r=0;require(sl_grass_reach(context,0,&r),"Grass reach unavailable");return double(r);};
  sl_environment_reset(context);frame(0);
  std::vector<float> crowd(30*6);for(int i=0;i<30;++i)std::copy(body.begin(),body.end(),crowd.begin()+i*6);
  require(sl_grass_contacts(context,crowd.data(),30),sl_error());frame(.01);require(std::abs(reachNow()-24.16)<.01,"Thirty contacts did not widen the grass shadow reach");
  require(sl_grass_contacts(context,body.data(),1),sl_error());frame(.1);frame(.4);require(reachNow()>24,"Grass shadow reach shrank while the blades were still settling");
  frame(.9);double relaxed=reachNow();require(relaxed<5&&relaxed>=.96-1e-4,"Grass shadow reach did not relax toward the remaining contact");
  frame(3);require(std::abs(reachNow()-.96)<.002,"Grass shadow reach dropped below, or stayed above, what one active contact needs");
  require(sl_grass_contacts(context,nullptr,0),sl_error());frame(3.1);frame(3.4);require(reachNow()>.95,"Grass shadow reach shrank while the blades were still settling after the contact left");
  frame(4);double tail=reachNow();require(tail>.16&&tail<.2,"Grass shadow reach did not relax after the contact left");
  frame(6);require(std::abs(reachNow()-.16)<.002,"Grass shadow reach never returned to its base");
  sl_environment_reset(context);frame(0);
 }
 // Grass pools hold a million vertices each: a pool left wholly unused must be freed once its retirement fences complete, and a later reload must work again.
 {
  auto poolBytes=[&]{uint64_t s[4]{};require(sl_grass_stats(context,s),"Grass stats unavailable");return s[3];};
  auto settleBelow=[&](uint64_t limit){for(int i=0;i<120&&poolBytes()>limit;++i)frame(0);return poolBytes();};
  const size_t placements=(size_t(1)<<20)/count+2;require(placements<4000,"Grass fixture too small to fill a pool");
  auto bigPacket=packet;uint32_t n=uint32_t(placements);memcpy(bigPacket.data()+4,&n,4);
  for(size_t i=1;i<placements;++i){size_t at=bigPacket.size();bigPacket.insert(bigPacket.end(),packet.begin()+12,packet.end());float x=float(matrix[12]+3.0*i);memcpy(bigPacket.data()+at+44+48,&x,4);}
  const uint64_t pool=64ull<<20;
  require(sl_scene(context,bigPacket.data(),int(bigPacket.size())),sl_error());frame(0);const uint64_t bigBytes=poolBytes();
  require(sl_scene(context,packet.data(),int(packet.size())),sl_error());const uint64_t smallBytes=settleBelow(bigBytes-pool);require(smallBytes+pool<=bigBytes,"Grass kept the second pool after the scene shrank");
  require(sl_scene(context,reinterpret_cast<unsigned char*>(empty),sizeof(empty)),sl_error());const uint64_t emptyBytes=settleBelow(smallBytes-pool);require(emptyBytes+pool<=smallBytes,"Grass kept its last pool after every instance was removed");
  require(sl_scene(context,bigPacket.data(),int(bigPacket.size())),sl_error());frame(0);const uint64_t againBytes=poolBytes();require(againBytes+(1ull<<20)>=bigBytes&&againBytes<=bigBytes+(1ull<<20),"Grass pools were not rebuilt to the same size after a reload");
  require(sl_scene(context,packet.data(),int(packet.size())),sl_error());require(sl_grass_contacts(context,nullptr,0),sl_error());sl_environment_reset(context);frame(0);require(read()==rest,"Grass geometry changed after the pool reclaim cycle");
 }
 return {{"success",true},{"vertices",count},{"movingContactAndRecoverySteps",120},{"peakTrajectoryError",peakError},{"peakRootError",peakRoot},{"peakNormalLengthError",peakNormal},{"peakNormalReferenceError",peakNormalReference},{"peakHistoryError",peakHistory},{"renderedMotionPeak",motion[0]},{"movingPixels",motion[2]},{"lateContactOffset",latePeak},{"instanceIsolationError",isolationError},{"resetVerified",true},{"unloadReloadVerified",true},{"pausedClockVerified",true}};
}
