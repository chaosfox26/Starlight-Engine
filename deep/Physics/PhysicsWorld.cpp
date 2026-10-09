#include <type_traits>
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Geometry/ConvexHullBuilder.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <cstdio>
#include <cfloat>
#include <map>
#include <future>
#include <array>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include <atomic>
#include <climits>
#include <cstring>
#include <string>
using namespace JPH;
namespace starlight {
struct BroadLayers : BroadPhaseLayerInterface {
 uint GetNumBroadPhaseLayers()const override{return 2;}
 BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer l)const override{return BroadPhaseLayer((uint8)l);}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
 const char* GetBroadPhaseLayerName(BroadPhaseLayer l)const override{return (uint8)l?"Dynamic":"Static";}
#endif
};
struct PairFilter : ObjectLayerPairFilter {bool ShouldCollide(ObjectLayer a,ObjectLayer b)const override{return a||b;}};
struct BroadFilter : ObjectVsBroadPhaseLayerFilter {bool ShouldCollide(ObjectLayer a,BroadPhaseLayer b)const override{return a||(uint8)b;}};
struct HullStats {
 std::atomic<uint64_t> requested{0},exact{0},finer{0},reduced{0},reducedOverBound{0},boxed{0},rejected{0},rebuilt{0};
 std::atomic<uint32_t> worstDeviationBits{0};std::atomic<float> worstRelative{0};
 std::mutex lock;std::string lastBox;
 void NoteReduced(float deviation,float diagonal,bool withinBound){
  ++reduced;if(!withinBound)++reducedOverBound;
  uint32_t bits;std::memcpy(&bits,&deviation,4);uint32_t seen=worstDeviationBits.load();while(bits>seen&&!worstDeviationBits.compare_exchange_weak(seen,bits)){}
  const float relative=diagonal>0?deviation/diagonal:0;float old=worstRelative.load();while(relative>old&&!worstRelative.compare_exchange_weak(old,relative)){}
 }
 void NoteBox(const char* message){++boxed;std::lock_guard<std::mutex> guard(lock);lastBox=message;}
};
static HullStats& HullCounters(){static HullStats* stats=new HullStats;return *stats;}
// Cooked collision shapes are immutable and in the body's own space, so one process-wide cache serves every physics world: a scene started again, or a
// second world with the same objects, reuses what an earlier one cooked. Entries no body holds any more (only the cache references them) are dropped when full.
struct SharedShapes {
 std::mutex lock;std::map<std::pair<uint64_t,uint64_t>,ShapeRefC> shapes;size_t bytes=0;
 std::atomic<uint64_t> hits{0},misses{0},trimmed{0},refused{0};
 static constexpr size_t kMaxShapes=4096;static constexpr size_t kMaxBytes=768ull<<20;
 void Trim(){
  for(auto it=shapes.begin();it!=shapes.end();){
   if(it->second->GetRefCount()==1){bytes-=std::min(bytes,it->second->GetStats().mSizeBytes);it=shapes.erase(it);++trimmed;}else ++it;
  }
 }
};
static SharedShapes& SharedShapeCache(){static SharedShapes* cache=new SharedShapes;return *cache;}
struct Liquid {uint32_t id;float height,ratio,padding;};
struct BodyState {uint32_t id;float x,y,z,qx,qy,qz,qw;};
class PhysicsWorld {
public:
 struct Contacts : ContactListener {
  std::mutex mutex;std::unordered_set<uint32_t> touching;
  void Collect(const Body& a,const Body& b){std::lock_guard<std::mutex> lock(mutex);if(a.IsDynamic())touching.insert(a.GetID().GetIndexAndSequenceNumber());if(b.IsDynamic())touching.insert(b.GetID().GetIndexAndSequenceNumber());}
  void OnContactAdded(const Body& a,const Body& b,const ContactManifold&,ContactSettings&)override{Collect(a,b);}
  void OnContactPersisted(const Body& a,const Body& b,const ContactManifold&,ContactSettings&)override{Collect(a,b);}
 } contacts;
 BroadLayers broad;PairFilter pairs;BroadFilter filter;TempAllocatorImpl temp{32*1024*1024};JobSystemThreadPool jobs{cMaxPhysicsJobs,cMaxPhysicsBarriers,4};PhysicsSystem world;
 struct Tile {BodyID body;std::array<float,289> heights{},requested{};std::future<ShapeRefC> pending;ShapeRefC cooked;bool valid=false;};
 BodyID volumeTerrain;
 uint64_t sharedShapeHits=0; // see Add()
 std::array<Tile,36> tiles;std::vector<BodyID> bodies,allProps;double accumulator=0;uint64_t ticks=0,cooks=0,droppedTicks=0;
 std::vector<BodyState> previousBodies;
 std::unordered_map<uint32_t,Vec3> liquidCurrents;
 struct Joint {BodyID source,target;Ref<TwoBodyConstraint> constraint;};
 std::vector<Joint> joints;
 #include "rope_physics.inl"
 void RemoveJoints(BodyID id){for(auto it=joints.begin();it!=joints.end();)if(it->source==id || it->target==id){world.RemoveConstraint(it->constraint);auto& api=world.GetBodyInterface();if(it->source!=id)api.ActivateBody(it->source);if(it->target!=id&&!it->target.IsInvalid())api.ActivateBody(it->target);it=joints.erase(it);}else ++it;}
 void AddJoint(uint32_t source,uint32_t target,int kind,const float* p){
  if(!p || source==target || kind<1 || kind>6)throw std::runtime_error("Invalid joint");
  for(int i=0;i<14;++i)if(!std::isfinite(p[i]))throw std::runtime_error("Non-finite joint setting");
  if(p[9]<=0 || p[10]<=0 || p[11]<0)throw std::runtime_error("Invalid joint limit");
  BodyID ids[]={BodyID(source),BodyID(target)};Ref<TwoBodyConstraint> joint;
  {BodyLockMultiWrite lock(world.GetBodyLockInterface(),ids,target==UINT32_MAX?1:2);
   auto* a=lock.GetBody(0);auto* b=target==UINT32_MAX?&Body::sFixedToWorld:lock.GetBody(1);
   if(!a || !b || !a->IsDynamic())throw std::runtime_error("Joint requires a live dynamic source");
   RVec3 anchorA(p[0],p[1],p[2]),anchorB(p[3],p[4],p[5]);
   if(kind==1){FixedConstraintSettings s;s.mAutoDetectPoint=true;joint=s.Create(*a,*b);}
   else if(kind==2){Vec3 axis(p[6],p[7],p[8]);if(axis.LengthSq()<1e-8f)throw std::runtime_error("Invalid hinge axis");axis=axis.Normalized();if(p[12]==p[13]){FixedConstraintSettings f;f.mAutoDetectPoint=true;joint=f.Create(*a,*b);}else{HingeConstraintSettings s;s.mPoint1=s.mPoint2=anchorA;s.mHingeAxis1=s.mHingeAxis2=axis;s.mNormalAxis1=s.mNormalAxis2=axis.GetNormalizedPerpendicular();s.mLimitsMin=p[12];s.mLimitsMax=p[13];joint=s.Create(*a,*b);}}
   else if(kind==5){PointConstraintSettings s;s.mPoint1=s.mPoint2=anchorA;joint=s.Create(*a,*b);}
   else if(kind==6){Vec3 axis(p[6],p[7],p[8]);if(axis.LengthSq()<1e-8f)throw std::runtime_error("Invalid slider axis");SliderConstraintSettings s;s.mAutoDetectPoint=true;s.SetSliderAxis(axis.Normalized());s.mLimitsMin=-p[9];s.mLimitsMax=p[9];joint=s.Create(*a,*b);}
   else {DistanceConstraintSettings s;s.mPoint1=anchorA;s.mPoint2=anchorB;s.mMinDistance=kind==3?0:p[9];s.mMaxDistance=p[9];if(kind==4)s.mLimitsSpringSettings=SpringSettings(ESpringMode::FrequencyAndDamping,p[10],p[11]);joint=s.Create(*a,*b);}
  }
  world.AddConstraint(joint);joints.push_back({ids[0],ids[1],joint});world.GetBodyInterface().ActivateBody(ids[0]);
 }

 Ref<CharacterVirtual> character;
 int characterJumps=0;
 Vec3 characterCarry=Vec3::sZero();
 float characterRadius=.3f,characterHeight=1.8f;
 float characterLiquidHeight=0,characterLiquidDensity=0;
 Vec3 characterCurrent=Vec3::sZero(),characterCurrentDrift=Vec3::sZero();
 PhysicsWorld(){world.Init(8192,0,32768,32768,broad,filter,pairs);world.SetContactListener(&contacts);}
 ~PhysicsWorld(){for(auto& tile:tiles)if(tile.pending.valid())tile.pending.wait();}
 void ClearProps(){for(auto& cable:cables)for(auto& constraint:cable.constraints)world.RemoveConstraint(constraint);cables.clear();for(auto& joint:joints)world.RemoveConstraint(joint.constraint);joints.clear();character=nullptr;auto& api=world.GetBodyInterface();for(auto id:allProps){api.RemoveBody(id);api.DestroyBody(id);}bodies.clear();previousBodies.clear();allProps.clear();liquidCurrents.clear();characterCurrent=characterCurrentDrift=Vec3::sZero();characterLiquidHeight=characterLiquidDensity=0;contacts.touching.clear();accumulator=0;ticks=0;}
 void StartCharacter(float radius,Vec3 feet){
  // A NaN start position never resets (every later step compares NaN), so the controller would stay NaN for good (audit2 F5). The radius range matches the scene's PlayerRadius.
  if(!std::isfinite(radius)||radius<.05f||radius>2.f||!std::isfinite(feet.GetX())||!std::isfinite(feet.GetY())||!std::isfinite(feet.GetZ()))throw std::runtime_error("Invalid character start");
  characterJumps=0;
  characterRadius=radius;characterHeight=1.8f;characterCarry=Vec3::sZero();characterCurrent=characterCurrentDrift=Vec3::sZero();
  // Terrain cooking is completed once at controller creation, never by sleeping.
  for(auto& tile:tiles)if(tile.pending.valid())tile.pending.wait();
  if(!Poll())throw std::runtime_error("Character terrain was not ready");
  CharacterVirtualSettings settings;
  const float halfHeight=std::max(.01f,.9f-radius);
  settings.mShape=new CapsuleShape(halfHeight,radius);
  settings.mShapeOffset=Vec3(0,halfHeight+radius,0);
  settings.mSupportingVolume=Plane(Vec3::sAxisY(),-radius);
  settings.mMaxSlopeAngle=DegreesToRadians(45.f);
  settings.mEnhancedInternalEdgeRemoval=true;
  character=new CharacterVirtual(&settings,RVec3(feet),Quat::sIdentity(),&world);
  character->RefreshContacts(world.GetDefaultBroadPhaseLayerFilter(1),world.GetDefaultLayerFilter(1),{},{},temp);
 }
 bool CharacterHeight(float height){
  if(!character || !std::isfinite(height) || height<.4f || height>1.8f)return false;
  if(std::abs(height-characterHeight)<.001f)return true;
  float radius=std::min(characterRadius,height*.45f),half=std::max(.01f,height*.5f-radius);
  RefConst<Shape> shape=new CapsuleShape(half,radius);
  auto oldOffset=character->GetShapeOffset();character->SetShapeOffset(Vec3(0,half+radius,0));
  if(!character->SetShape(shape,.02f,world.GetDefaultBroadPhaseLayerFilter(1),world.GetDefaultLayerFilter(1),{},{},temp)){
   character->SetShapeOffset(oldOffset);return false;
  }
  characterHeight=height;return true;
 }
 bool CharacterTraverse(Vec3 feet,Vec3 travel){
  if(!character || !std::isfinite(feet.LengthSq()) || !std::isfinite(travel.LengthSq()))return false;
  CapsuleShape capsule(std::max(.01f,.9f-characterRadius),characterRadius);
  RShapeCast cast(&capsule,Vec3::sOne(),RMat44::sTranslation(RVec3(feet+Vec3(0,.9f,0))),travel);
  ShapeCastSettings settings;ClosestHitCollisionCollector<CastShapeCollector> hits;
  world.GetNarrowPhaseQuery().CastShape(cast,settings,RVec3::sZero(),hits,world.GetDefaultBroadPhaseLayerFilter(1),world.GetDefaultLayerFilter(1));
  return !hits.HadHit();
 }
 // Coulomb-style rolling resistance acts only on bodies with recent contacts.
 // Damping above remains a separate, small aerodynamic/numerical loss.
 void ApplyRollingResistance(){
  auto& api=world.GetBodyInterface();
  for(auto raw:contacts.touching){BodyID id(raw);float torque=0;Vec3 omega=api.GetAngularVelocity(id);float speed=omega.Length();if(speed<.001f)continue;{BodyLockRead lock(world.GetBodyLockInterface(),id);if(lock.Succeeded()){auto& b=lock.GetBody();float inv=b.GetMotionProperties()->GetInverseMass();float radius=b.GetShape()->GetLocalBounds().GetExtent().Length();if(inv>0)torque=.012f/inv*9.81f*radius*std::min(1.f,speed);}}if(torque>0)api.AddTorque(id,-omega/speed*torque);}
  contacts.touching.clear();
 }
 // The adventure world advances only through CharacterStep, which never reached the rolling resistance above (audit 03#7); STARLIGHT_ROLLING_RESISTANCE=1 applies it there too. Default off keeps the shipped roll-out behaviour.
 static bool RollingResistanceInCharacterStep(){char v[8]{};size_t n=0;return getenv_s(&n,v,sizeof v,"STARLIGHT_ROLLING_RESISTANCE")==0&&v[0]=='1';}
 void CharacterStep(float dt,Vec3 feet,Vec3 input,bool jump,float* output,bool overrideMotion=false){
  if(!character || !std::isfinite(dt) || dt<=0 || dt>.051f || !std::isfinite(feet.GetX()+feet.GetY()+feet.GetZ()) || !std::isfinite(input.GetX()+input.GetY()+input.GetZ()))throw std::runtime_error("Invalid character step");
  if((character->GetPosition()-RVec3(feet)).LengthSq()>1.e-6){
   character->SetPosition(RVec3(feet));character->SetLinearVelocity(Vec3::sZero());
   character->RefreshContacts(world.GetDefaultBroadPhaseLayerFilter(1),world.GetDefaultLayerFilter(1),{},{},temp);
  }
  character->UpdateGroundVelocity();
  Vec3 ground=character->GetGroundVelocity();
  bool supported=character->GetGroundState()==CharacterBase::EGroundState::OnGround;
  bool descending=character->GetLinearVelocity().GetY()-ground.GetY()<.1f;
  Vec3 velocity(characterCarry.GetX(),character->GetLinearVelocity().GetY(),characterCarry.GetZ());
  if(supported && descending){velocity=ground;characterJumps=0;characterCarry=Vec3(ground.GetX(),0,ground.GetZ());}
  if(overrideMotion)characterCarry=Vec3::sZero();
  // Each request is a fresh input press. Walking off a ledge leaves one air
  // jump; landing restores both. Air jump replaces vertical speed, so falling
  // does not consume the second jump without providing upward motion.
  if(!supported && characterJumps==0)characterJumps=1;
  const float submerged=characterLiquidDensity>0?std::clamp((characterLiquidHeight-feet.GetY())/characterHeight,0.f,1.f):0.f;
  if(jump && submerged>.35f){velocity.SetY(3.f);characterJumps=0;}
  else if(jump && characterJumps<2){velocity.SetY(ground.GetY()+5.f);++characterJumps;}
  Vec3 acceleration=world.GetGravity()*(1.f-submerged*characterLiquidDensity/950.f);
  if(submerged>0){velocity*=std::exp(-2.5f*submerged*dt);input*=1.f-.55f*submerged;}
  if(overrideMotion)characterCurrentDrift=Vec3::sZero();
  else if(submerged>0)characterCurrentDrift=characterCurrent+(characterCurrentDrift-characterCurrent)*std::exp(-2.5f*submerged*dt);
  else characterCurrentDrift*=std::exp(-(supported?8.f:.2f)*dt);
  character->SetLinearVelocity(overrideMotion?input:velocity+input+acceleration*dt+characterCurrentDrift);
  CharacterVirtual::ExtendedUpdateSettings settings;
  settings.mWalkStairsStepUp=Vec3(0,.4f,0);
  settings.mStickToFloorStepDown=(overrideMotion || jump || submerged>.2f || velocity.GetY()>ground.GetY()+.1f)?Vec3::sZero():Vec3(0,-.4f,0);
  if(overrideMotion)settings.mWalkStairsStepUp=Vec3::sZero();
  character->ExtendedUpdate(dt,world.GetGravity(),settings,world.GetDefaultBroadPhaseLayerFilter(1),world.GetDefaultLayerFilter(1),{},{},temp);
  if(RollingResistanceInCharacterStep())ApplyRollingResistance();
  if(world.Update(dt,1,&temp,&jobs)!=EPhysicsUpdateError::None)throw std::runtime_error("Character world capacity exceeded");
  auto p=character->GetPosition();output[0]=(float)p.GetX();output[1]=(float)p.GetY();output[2]=(float)p.GetZ();output[3]=character->GetGroundState()==CharacterBase::EGroundState::OnGround?1.f:0.f;
 }
 static void SetInjectedCookFailures(int n){InjectedCookFailures().store(n);}
 static std::atomic<int>& InjectedCookFailures(){static std::atomic<int> remaining{0};return remaining;}
 static void SetInjectedUpdateFailures(int n){InjectedUpdateFailures().store(n);}
 static std::atomic<int>& InjectedUpdateFailures(){static std::atomic<int> remaining{0};return remaining;}
 static void SetInjectedAddFailures(int n){InjectedAddFailures().store(n);}
 static std::atomic<int>& InjectedAddFailures(){static std::atomic<int> remaining{0};return remaining;}
 static ShapeRefC Cook(const std::array<float,289>& h,int tx,int tz){
  {auto& remaining=InjectedCookFailures();int n=remaining.load();while(n>0&&!remaining.compare_exchange_weak(n,n-1)){}if(n>0)throw std::runtime_error("injected terrain cook failure");}
  TriangleList triangles;triangles.reserve(512);
  auto point=[&](int x,int z){return Float3(-12+(tx*16+x)*.25f,h[z*17+x],-12+(tz*16+z)*.25f);};
  for(int z=0;z<16;z++)for(int x=0;x<16;x++){auto a=point(x,z),b=point(x+1,z),c=point(x,z+1),d=point(x+1,z+1);triangles.emplace_back(a,c,b);triangles.emplace_back(b,c,d);}
  MeshShapeSettings settings(triangles);auto result=settings.Create();if(result.HasError())throw std::runtime_error(result.GetError().c_str());return result.Get();
 }
 // Jolt compiles this module with _HAS_EXCEPTIONS=0, so an exception on a std::async worker ends the process. The worker reports a failed cook as a null shape instead.
 static ShapeRefC CookGuarded(const std::array<float,289>& h,int tx,int tz){try{return Cook(h,tx,tz);}catch(const std::exception& e){std::fprintf(stderr,"terrain cook failed: %s\n",e.what());return ShapeRefC();}}
 void Terrain(const float* heights){
  // Checked before anything is changed: a refused update must not remove the volumetric terrain it was meant to replace (audit2 F3).
  for(int i=0;i<97*97;++i)if(!std::isfinite(heights[i]))throw std::runtime_error("Invalid terrain height");
  if(!volumeTerrain.IsInvalid()){auto& api=world.GetBodyInterface();api.RemoveBody(volumeTerrain);api.DestroyBody(volumeTerrain);volumeTerrain=BodyID();}
  Poll();for(int tz=0;tz<6;tz++)for(int tx=0;tx<6;tx++){auto& t=tiles[tz*6+tx];std::array<float,289> data;for(int z=0;z<17;z++)for(int x=0;x<17;x++)data[z*17+x]=heights[(tz*16+z)*97+tx*16+x];t.requested=data;if(t.pending.valid()||(t.valid&&t.heights==data))continue;t.heights=data;t.valid=true;t.cooked=ShapeRefC();t.pending=std::async(std::launch::async,[data,tx,tz]{return CookGuarded(data,tx,tz);});cooks++;}
 }
 void TerrainMesh(const float* points,int count,const uint32_t* indices,int indexCount){
  if(!points || !indices || count<4 || count>1000000 || indexCount<3 || indexCount%3 || indexCount>6000000)throw std::runtime_error("Invalid volumetric terrain mesh");
  for(int i=0;i<count*3;++i)if(!std::isfinite(points[i]))throw std::runtime_error("Non-finite volumetric terrain vertex");
  TriangleList triangles;triangles.reserve(indexCount/3);
  for(int i=0;i<indexCount;i+=3){Float3 p[3];for(int k=0;k<3;++k){auto v=indices[i+k];if(v>=uint32_t(count))throw std::runtime_error("Invalid volumetric terrain index");p[k]=Float3(points[v*3],points[v*3+1],points[v*3+2]);}triangles.emplace_back(p[0],p[1],p[2]);}
  MeshShapeSettings shape(triangles);auto cooked=shape.Create();if(cooked.HasError())throw std::runtime_error(cooked.GetError().c_str());
  auto& api=world.GetBodyInterface();BodyCreationSettings settings(cooked.Get(),RVec3::sZero(),Quat::sIdentity(),EMotionType::Static,0);settings.mFriction=.72f;
  auto next=api.CreateAndAddBody(settings,EActivation::DontActivate);if(next.IsInvalid())throw std::runtime_error("Terrain body capacity reached");
  for(auto& tile:tiles){if(tile.pending.valid()){tile.pending.wait();tile.pending={};}if(!tile.body.IsInvalid()){api.RemoveBody(tile.body);api.DestroyBody(tile.body);}tile=Tile{};}
  if(!volumeTerrain.IsInvalid()){api.RemoveBody(volumeTerrain);api.DestroyBody(volumeTerrain);}volumeTerrain=next;
  // Removing a static support does not automatically wake resting props.
  // Retain their bodies, velocities and constraints, then let them settle.
  for(auto id:bodies)api.ActivateBody(id);
 }
 bool Poll(){if(!volumeTerrain.IsInvalid())return true;bool ready=true;auto& api=world.GetBodyInterface();for(auto& t:tiles){if(t.pending.valid()){if(t.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready){t.cooked=t.pending.get();if(!t.cooked){t.valid=false;t.heights={};throw std::runtime_error("Terrain tile cook failed; the tile is cooked again on the next terrain update");}}else ready=false;}if(t.cooked){if(t.body.IsInvalid()){BodyCreationSettings settings(t.cooked,RVec3::sZero(),Quat::sIdentity(),EMotionType::Static,0);settings.mFriction=.72f;t.body=api.CreateAndAddBody(settings,EActivation::DontActivate);if(t.body.IsInvalid())throw std::runtime_error("Terrain body capacity reached");}else api.SetShape(t.body,t.cooked,true,EActivation::Activate);t.cooked=ShapeRefC();}if(t.valid&&!t.pending.valid()&&t.requested!=t.heights){t.heights=t.requested;auto data=t.heights;int index=(int)(&t-tiles.data());t.pending=std::async(std::launch::async,[data,index]{return CookGuarded(data,index%6,index/6);});cooks++;ready=false;}if(t.body.IsInvalid())ready=false;}return ready;}
 struct HullInfo {int kind=0,inputPoints=0,hullPoints=0;float deviation=0,diagonal=0;}; // kind: 0 exact, 1 exact after a finer-tolerance retry, 2 reduced to the 256-vertex limit (deviation measured), 3 bounding box, 4 rebuilt from the builder's own hull vertices (deviation measured)
 // Distance from a point to a triangle (closest point on the triangle), in double precision.
 static double PointTriangleDistance(const Vec3& p,const Vec3& a,const Vec3& b,const Vec3& c){
  using V=std::array<double,3>;
  auto d=[](const Vec3& v){return V{v.GetX(),v.GetY(),v.GetZ()};};
  auto sub=[](V x,V y){return V{x[0]-y[0],x[1]-y[1],x[2]-y[2]};};
  auto dot=[](V x,V y){return x[0]*y[0]+x[1]*y[1]+x[2]*y[2];};
  const V A=d(a),B=d(b),C=d(c),P=d(p);
  const V ab=sub(B,A),ac=sub(C,A),ap=sub(P,A);
  auto distance=[&](V q){auto r=sub(P,q);return std::sqrt(dot(r,r));};
  const double d1=dot(ab,ap),d2=dot(ac,ap);
  if(d1<=0&&d2<=0)return distance(A);
  const V bp=sub(P,B);const double d3=dot(ab,bp),d4=dot(ac,bp);
  if(d3>=0&&d4<=d3)return distance(B);
  const double vc=d1*d4-d3*d2;
  if(vc<=0&&d1>=0&&d3<=0){const double v=d1/(d1-d3);return distance({A[0]+v*ab[0],A[1]+v*ab[1],A[2]+v*ab[2]});}
  const V cp=sub(P,C);const double d5=dot(ab,cp),d6=dot(ac,cp);
  if(d6>=0&&d5<=d6)return distance(C);
  const double vb=d5*d2-d1*d6;
  if(vb<=0&&d2>=0&&d6<=0){const double w=d2/(d2-d6);return distance({A[0]+w*ac[0],A[1]+w*ac[1],A[2]+w*ac[2]});}
  const double va=d3*d6-d5*d4;
  if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0){const double w=(d4-d3)/((d4-d3)+(d5-d6));return distance({B[0]+w*(C[0]-B[0]),B[1]+w*(C[1]-B[1]),B[2]+w*(C[2]-B[2])});}
  const double denom=1.0/(va+vb+vc),v=vb*denom,w=vc*denom;
  return distance({A[0]+ab[0]*v+ac[0]*w,A[1]+ab[1]*v+ac[1]*w,A[2]+ab[2]*v+ac[2]*w});
 }
 // Largest distance from any input point to the convex hull that `builder` built over `hull`'s positions (zero when every point is inside it). A hull made of
 // some of the input points lies inside the true hull of all of them, so this is the exact one-sided Hausdorff distance between the two hulls.
 static float HullDeviation(const Array<Vec3>& input,const Array<Vec3>& hull,const ConvexHullBuilder& builder){
  struct Plane {Vec3 normal,origin;};struct Triangle {Vec3 a,b,c;};
  std::vector<Plane> planes;std::vector<Triangle> triangles;
  for(const auto* face:builder.GetFaces()){
   planes.push_back({face->mNormal.Normalized(),face->mCentroid});
   const auto* first=face->mFirstEdge;const Vec3 a=hull[first->mStartIdx];
   for(const auto* edge=first->mNextEdge;edge->mNextEdge!=first;edge=edge->mNextEdge)triangles.push_back({a,hull[edge->mStartIdx],hull[edge->mNextEdge->mStartIdx]});
  }
  float extent=1.f;for(const Vec3& p:input)extent=std::max({extent,std::abs(p.GetX()),std::abs(p.GetY()),std::abs(p.GetZ())});
  const float inside=1.0e-6f*extent;double worst=0;
  for(const Vec3& p:input){
   float top=-FLT_MAX;for(const auto& plane:planes)top=std::max(top,plane.normal.Dot(p-plane.origin));
   if(top<=inside)continue;
   double best=DBL_MAX;for(const auto& t:triangles)best=std::min(best,PointTriangleDistance(p,t.a,t.b,t.c));
   worst=std::max(worst,best);
  }
  return float(worst);
 }
 static float ShapeDeviation(const Array<Vec3>& input,const ConvexHullShape& shape){
  Array<Vec3> hull;const Vec3 center=shape.GetCenterOfMass();for(uint i=0;i<shape.GetNumPoints();++i)hull.push_back(shape.GetPoint(i)+center);
  ConvexHullBuilder builder(hull);const char* error=nullptr;
  const auto status=builder.Initialize(INT_MAX,1.0e-6f,error);
  if(status!=ConvexHullBuilder::EResult::Success&&status!=ConvexHullBuilder::EResult::MaxVerticesReached)return FLT_MAX;
  return HullDeviation(input,hull,builder);
 }
 // A hull reduced to the library's vertex limit is accepted only when no input point lies farther than this outside it.
 static float ReducedHullBound(float diagonal){return std::max(.005f*diagonal,.001f);}
 // Builds the dynamic convex-hull shape for a point set (including the finer-tolerance retry). It touches no physics world,
 // so it is safe to run on any thread once the Jolt allocator is registered (see EnsureJoltInitialized).
 static ShapeRefC BuildHull(const float* points,int count,float density,HullInfo* info=nullptr){
  HullInfo local;HullInfo& out=info?*info:local;out=HullInfo{};out.inputPoints=count;
  auto& counters=HullCounters();++counters.requested;
  Array<Vec3> vertices;vertices.reserve(count);for(int i=0;i<count;i++)vertices.emplace_back(points[i*3],points[i*3+1],points[i*3+2]);
  Vec3 low(FLT_MAX,FLT_MAX,FLT_MAX),high(-FLT_MAX,-FLT_MAX,-FLT_MAX);bool finite=true;
  for(const auto& v:vertices){if(!std::isfinite(v.GetX())||!std::isfinite(v.GetY())||!std::isfinite(v.GetZ())){finite=false;break;}low=Vec3::sMin(low,v);high=Vec3::sMax(high,v);}
  // Refused before the hull builder runs: it can return a hull for NaN vertices, which would make a body with a NaN shape (audit2 F6).
  if(!finite){++counters.rejected;throw std::runtime_error("Nonfinite collision vertex");}
  out.diagonal=finite?(high-low).Length():0.f;
  ConvexHullShapeSettings settings(vertices);settings.mDensity=density;auto result=settings.Create();
  std::string overBound;
  if(!result.HasError()){
   ShapeRefC shape=result.Get();
   out.hullPoints=int(static_cast<const ConvexHullShape*>(shape.GetPtr())->GetNumPoints());
   if(finite&&out.hullPoints>=ConvexHullShape::cMaxPointsInHull){
    // The library stops adding vertices at its limit and still reports success; the hull is then the farthest-point reduction of the real one.
    ConvexHullBuilder probe(vertices);const char* probeError=nullptr;
    if(probe.Initialize(ConvexHullShape::cMaxPointsInHull,settings.mHullTolerance,probeError)==ConvexHullBuilder::EResult::MaxVerticesReached){
     out.kind=2;out.deviation=HullDeviation(vertices,vertices,probe);const bool within=out.deviation<=ReducedHullBound(out.diagonal);
     if(within){counters.NoteReduced(out.deviation,out.diagonal,true);return shape;}
     overBound="the first reduced hull lies "+std::to_string(out.deviation)+" from the farthest point (limit "+std::to_string(ReducedHullBound(out.diagonal))+")";out.kind=0;out.deviation=0;
    }
   }
   if(overBound.empty()){++counters.exact;return shape;}
  }
  std::string reason=result.HasError()?std::string(result.GetError().c_str()):overBound;
  // Preserve successful authored collision hulls. A failed coarse hull can
  // omit an exterior face; retry the entire same point set at finer accuracy.
  // Reject an exhausted vertex budget instead of silently accepting its error.
  constexpr float finerTolerance=1.0e-4f;
  // The builder can finish with a face structure whose merged faces are not all supporting planes of its own vertices; the library then rejects the hull
  // although the vertices it chose lie within the tolerance of every input point. The hull of exactly those vertices is rebuilt and used when the
  // farthest input point is within the bound of it, measured on the shape that will really be used.
  auto rebuild=[&](const ConvexHullBuilder& source)->ShapeRefC{
   std::vector<char> used(vertices.size(),0);Array<Vec3> subset;
   for(const auto* face:source.GetFaces()){const auto* edge=face->mFirstEdge;do{if(!used[edge->mStartIdx]){used[edge->mStartIdx]=1;subset.push_back(vertices[edge->mStartIdx]);}edge=edge->mNextEdge;}while(edge!=face->mFirstEdge);}
   if(subset.size()<4||subset.size()>size_t(ConvexHullShape::cMaxPointsInHull)){reason+="; rebuild skipped ("+std::to_string(subset.size())+" hull vertices)";return ShapeRefC();}
   ConvexHullShapeSettings made(subset);made.mDensity=density;made.mHullTolerance=finerTolerance;auto built=made.Create();
   if(built.HasError()){reason+="; rebuild from the builder's own vertices: "+std::string(built.GetError().c_str());return ShapeRefC();}
   ShapeRefC shape=built.Get();const auto* convex=static_cast<const ConvexHullShape*>(shape.GetPtr());
   const float deviation=ShapeDeviation(vertices,*convex),bound=ReducedHullBound(out.diagonal);
   if(!(deviation<=bound)){reason+="; rebuilt hull would lie "+std::to_string(deviation)+" from the farthest point (limit "+std::to_string(bound)+")";return ShapeRefC();}
   out.kind=4;out.hullPoints=int(convex->GetNumPoints());out.deviation=deviation;++counters.rebuilt;
   return shape;
  };
  if(finite){
   const char* hullError=nullptr;ConvexHullBuilder probe(vertices);
   const auto status=probe.Initialize(ConvexHullShape::cMaxPointsInHull,finerTolerance,hullError);
   if(status==ConvexHullBuilder::EResult::Success){
    ConvexHullBuilder::Face* face=nullptr;float error=0,coplanar=0;int point=-1;
    probe.DetermineMaxError(face,error,point,coplanar);
    if(std::isfinite(error)&&error<=4.0f*std::max(coplanar,finerTolerance)){
     ConvexHullShapeSettings retry(vertices);retry.mDensity=density;retry.mHullTolerance=finerTolerance;auto retried=retry.Create();
     if(!retried.HasError()){out.kind=1;out.hullPoints=int(static_cast<const ConvexHullShape*>(retried.Get().GetPtr())->GetNumPoints());++counters.finer;return retried.Get();}
     reason+="; finer retry: "+std::string(retried.GetError().c_str());
    }else reason+="; finer retry measured an error of "+std::to_string(error);
    if(ShapeRefC shape=rebuild(probe))return shape;
   }else if(status==ConvexHullBuilder::EResult::MaxVerticesReached){
    // The real hull needs more than the library's 256 vertices. The builder has already chosen vertices farthest-first; the hull of exactly those is the
    // reduction, and it is used only when the farthest input point is within the bound of it (measured on the shape that will really be used).
    std::vector<char> used(vertices.size(),0);Array<Vec3> subset;
    for(const auto* face:probe.GetFaces()){const auto* edge=face->mFirstEdge;do{if(!used[edge->mStartIdx]){used[edge->mStartIdx]=1;subset.push_back(vertices[edge->mStartIdx]);}edge=edge->mNextEdge;}while(edge!=face->mFirstEdge);}
    if(subset.size()>=4&&subset.size()<=size_t(ConvexHullShape::cMaxPointsInHull)){
     ConvexHullShapeSettings reduced(subset);reduced.mDensity=density;reduced.mHullTolerance=finerTolerance;auto made=reduced.Create();
     if(!made.HasError()){
      ShapeRefC shape=made.Get();const auto* convex=static_cast<const ConvexHullShape*>(shape.GetPtr());
      out.hullPoints=int(convex->GetNumPoints());out.deviation=ShapeDeviation(vertices,*convex);
      const float bound=ReducedHullBound(out.diagonal);
      if(out.deviation<=bound){out.kind=2;counters.NoteReduced(out.deviation,out.diagonal,true);return shape;}
      reason+="; the real hull needs more than 256 vertices and the reduced hull would lie "+std::to_string(out.deviation)+" from the farthest point (limit "+std::to_string(bound)+")";
     }else reason+="; reduced hull: "+std::string(made.GetError().c_str());
    }else reason+="; the real hull needs more than 256 vertices and the reduction produced "+std::to_string(subset.size())+" vertices";
   }else reason+="; finer builder said: "+std::string(hullError?hullError:"unknown");
   // Last stage before the box: ask the builder for a coarser hull whose tolerance stays well inside the accepted bound, rebuild the shape from the vertices it
   // chose, and use it only when the farthest input point is within the bound of the shape that will really be used.
   const float bound=ReducedHullBound(out.diagonal);
   for(const float fraction:{.0005f,.001f,.002f,.003f}){
    const float tolerance=std::max(fraction*out.diagonal,finerTolerance);if(tolerance>.5f*bound)break;
    const char* coarseError=nullptr;ConvexHullBuilder coarse(vertices);
    const auto state=coarse.Initialize(ConvexHullShape::cMaxPointsInHull,tolerance,coarseError);
    if(state!=ConvexHullBuilder::EResult::Success&&state!=ConvexHullBuilder::EResult::MaxVerticesReached)continue;
    std::vector<char> used(vertices.size(),0);Array<Vec3> subset;
    for(const auto* face:coarse.GetFaces()){const auto* edge=face->mFirstEdge;do{if(!used[edge->mStartIdx]){used[edge->mStartIdx]=1;subset.push_back(vertices[edge->mStartIdx]);}edge=edge->mNextEdge;}while(edge!=face->mFirstEdge);}
    if(subset.size()<4||subset.size()>size_t(ConvexHullShape::cMaxPointsInHull))continue;
    ConvexHullShapeSettings coarseSettings(subset);coarseSettings.mDensity=density;coarseSettings.mHullTolerance=tolerance;auto coarseBuilt=coarseSettings.Create();
    if(coarseBuilt.HasError())continue;
    ShapeRefC shape=coarseBuilt.Get();const auto* convex=static_cast<const ConvexHullShape*>(shape.GetPtr());
    const float deviation=ShapeDeviation(vertices,*convex);
    if(deviation<=bound){out.kind=2;out.hullPoints=int(convex->GetNumPoints());out.deviation=deviation;counters.NoteReduced(deviation,out.diagonal,true);return shape;}
   }
  }
  // A point set the hull builder cannot represent within tolerance must not stop the game: the body falls back to the oriented-free
  // bounding box of the same points (same density), and the event is logged and counted so a recurring cause can be found. Non-finite points
  // are still rejected, because no shape can be built from them.
  if(!finite){++counters.rejected;throw std::runtime_error(reason.c_str());}
  const Vec3 half=Vec3::sMax((high-low)*.5f,Vec3::sReplicate(.005f));
  char message[512];std::snprintf(message,sizeof(message),"hull fallback to box: %d points, extent %.3f x %.3f x %.3f, builder said: %s",count,2*half.GetX(),2*half.GetY(),2*half.GetZ(),reason.c_str());
  std::fprintf(stderr,"%s\n",message);counters.NoteBox(message);
  out.kind=3;
  BoxShapeSettings box(half);box.mDensity=density;
  RotatedTranslatedShapeSettings placed(( low+high)*.5f,Quat::sIdentity(),box.Create().Get());
  auto fallback=placed.Create();if(fallback.HasError())throw std::runtime_error(fallback.GetError().c_str());return fallback.Get();
 }
 static void CheckPose(Vec3 pos,Quat rotation){
  for(float value:{pos.GetX(),pos.GetY(),pos.GetZ(),rotation.GetX(),rotation.GetY(),rotation.GetZ(),rotation.GetW()})if(!std::isfinite(value))throw std::runtime_error("Nonfinite body pose");
  if(rotation.LengthSq()<1e-10f)throw std::runtime_error("Invalid body rotation");
 }
 uint32_t AddShape(ShapeRefC shape,bool dynamic,Vec3 pos,Quat rotation){
  CheckPose(pos,rotation);
  {auto& remaining=InjectedAddFailures();int n=remaining.load();while(n>0&&!remaining.compare_exchange_weak(n,n-1)){}if(n>0)throw std::runtime_error("injected body add failure");}
  BodyCreationSettings settings(shape,RVec3(pos),rotation.Normalized(),dynamic?EMotionType::Dynamic:EMotionType::Static,dynamic?1:0);settings.mFriction=.72f;settings.mRestitution=.03f;settings.mAngularDamping=.18f;settings.mLinearDamping=.03f;settings.mMotionQuality=dynamic?EMotionQuality::LinearCast:EMotionQuality::Discrete;
  BodyID id=world.GetBodyInterface().CreateAndAddBody(settings,dynamic?EActivation::Activate:EActivation::DontActivate);if(id.IsInvalid())throw std::runtime_error("Body capacity reached");allProps.push_back(id);if(dynamic)bodies.push_back(id);return id.GetIndexAndSequenceNumber();
 }
 uint32_t Add(const float* points,int count,const uint32_t* indices,int indexCount,bool dynamic,float density,Vec3 pos,Quat rotation){
  if(!points||!indices||count<4||count>200000||indexCount<3||indexCount>6000000||indexCount%3)throw std::runtime_error("Invalid collision mesh");
  CheckPose(pos,rotation);if(!std::isfinite(density)||(dynamic&&density<=0))throw std::runtime_error("Invalid body density");
  ShapeRefC shape;
  // Collision shapes are immutable and in the body's own space, so bodies with the same points, indices, density and kind share ONE shape instead of
  // cooking one each (a field of identical rocks cooks once). Two independent 64-bit hashes of the full input make a false match negligible.
  uint64_t keyA=1469598103934665603ull,keyB=0x9E3779B97F4A7C15ull;
  auto mix=[&](const void* data,size_t bytes){auto p=static_cast<const unsigned char*>(data);for(size_t i=0;i<bytes;++i){keyA^=p[i];keyA*=1099511628211ull;keyB=(keyB^p[i])*0xff51afd7ed558ccdull;keyB^=keyB>>29;}};
  mix(points,size_t(count)*3*sizeof(float));mix(indices,size_t(indexCount)*sizeof(uint32_t));mix(&dynamic,sizeof(dynamic));mix(&density,sizeof(density));
  const auto shapeKey=std::make_pair(keyA,keyB);
  auto& shared=SharedShapeCache();
  {
   std::lock_guard<std::mutex> guard(shared.lock);
   if(auto found=shared.shapes.find(shapeKey);found!=shared.shapes.end()){++sharedShapeHits;++shared.hits;return AddShape(found->second,dynamic,pos,rotation);}
  }
  ++shared.misses;
  if(dynamic)shape=BuildHull(points,count,density);
  else {TriangleList triangles;for(int i=0;i<indexCount;i+=3){Float3 p[3];for(int j=0;j<3;j++){auto k=indices[i+j];if(k>=(uint32_t)count)throw std::runtime_error("Invalid collision index");if(!std::isfinite(points[k*3])||!std::isfinite(points[k*3+1])||!std::isfinite(points[k*3+2]))throw std::runtime_error("Nonfinite collision vertex");p[j]=Float3(points[k*3],points[k*3+1],points[k*3+2]);}triangles.emplace_back(p[0],p[1],p[2]);}MeshShapeSettings settings(triangles);auto result=settings.Create();if(result.HasError())throw std::runtime_error(result.GetError().c_str());shape=result.Get();}
  {
   std::lock_guard<std::mutex> guard(shared.lock);
   if(auto found=shared.shapes.find(shapeKey);found!=shared.shapes.end())shape=found->second;
   else{
    const size_t size=shape->GetStats().mSizeBytes;
    if(shared.shapes.size()>=SharedShapes::kMaxShapes||shared.bytes+size>SharedShapes::kMaxBytes)shared.Trim();
    if(shared.shapes.size()<SharedShapes::kMaxShapes&&shared.bytes+size<=SharedShapes::kMaxBytes){shared.shapes.emplace(shapeKey,shape);shared.bytes+=size;}
    else ++shared.refused;
   }
  }
  return AddShape(shape,dynamic,pos,rotation);
 }
 // Presentation interpolates completed fixed ticks; collision state stays authoritative.
 int Present(BodyState* output,int capacity){
  auto& api=world.GetBodyInterface();float alpha=std::clamp(float(accumulator*60),0.f,1.f);int n=0;
  for(auto id:bodies){if(n>=capacity)break;RVec3 p;Quat q;api.GetPositionAndRotation(id,p,q);
   BodyState current{id.GetIndexAndSequenceNumber(),(float)p.GetX(),(float)p.GetY(),(float)p.GetZ(),q.GetX(),q.GetY(),q.GetZ(),q.GetW()};
   if(size_t(n)<previousBodies.size() && previousBodies[n].id==current.id){auto a=previousBodies[n];
    current.x=a.x+(current.x-a.x)*alpha;current.y=a.y+(current.y-a.y)*alpha;current.z=a.z+(current.z-a.z)*alpha;
    float sign=a.qx*current.qx+a.qy*current.qy+a.qz*current.qz+a.qw*current.qw<0?-1.f:1.f;
    float x=a.qx+(sign*current.qx-a.qx)*alpha,y=a.qy+(sign*current.qy-a.qy)*alpha,z=a.qz+(sign*current.qz-a.qz)*alpha,w=a.qw+(sign*current.qw-a.qw)*alpha;
    float inv=1/std::sqrt(x*x+y*y+z*z+w*w);current.qx=x*inv;current.qy=y*inv;current.qz=z*inv;current.qw=w*inv;
   }
   output[n++]=current;
  }return n;
 }
 int Step(double elapsed,const Liquid* liquid,int liquidCount,BodyState* output,int capacity){
  if(!std::isfinite(elapsed)||elapsed<0||elapsed>1)throw std::runtime_error("Invalid physics elapsed time");if(!Poll())return 0;accumulator+=elapsed;auto& api=world.GetBodyInterface();int steps=0;
  while(accumulator>=1.0/60&&steps++<12){
   for(auto& cable:cables)cable.previous=CablePoints(cable);
   previousBodies.clear();for(auto id:bodies){RVec3 p;Quat q;api.GetPositionAndRotation(id,p,q);previousBodies.push_back({id.GetIndexAndSequenceNumber(),(float)p.GetX(),(float)p.GetY(),(float)p.GetZ(),q.GetX(),q.GetY(),q.GetZ(),q.GetW()});}
   for(int i=0;i<liquidCount;i++){auto f=liquid[i];BodyID id(f.id);if(f.ratio>0)api.ApplyBuoyancyImpulse(id,RVec3(0,f.height,0),Vec3::sAxisY(),f.ratio,.9f,.3f,liquidCurrents.count(f.id)?liquidCurrents.at(f.id):Vec3::sZero(),world.GetGravity(),1.f/60);}
   ApplyRollingResistance();
   const bool injected=InjectedUpdateFailures().load()>0&&InjectedUpdateFailures().fetch_sub(1)>0;auto error=injected?EPhysicsUpdateError::ManifoldCacheFull:world.Update(1.f/60,1,&temp,&jobs);
   // A failed update must not leave its time behind: the backlog would be replayed on the next call (and grow while the failure lasts) instead of the step simply failing again or recovering.
   if(error!=EPhysicsUpdateError::None){accumulator=0;throw std::runtime_error("Jolt capacity exceeded during step");}accumulator-=1.0/60;ticks++;
  }
  // Time the simulation cannot catch up on is dropped beyond a quarter of a second, and counted, so a slow machine does not replay an ever-growing backlog.
  if(accumulator>.25){droppedTicks+=uint64_t(std::llround((accumulator-.25)*60));accumulator=.25;}
  int n=0;for(auto id:bodies){if(n>=capacity)break;RVec3 p;Quat q;api.GetPositionAndRotation(id,p,q);output[n++]={id.GetIndexAndSequenceNumber(),(float)p.GetX(),(float)p.GetY(),(float)p.GetZ(),q.GetX(),q.GetY(),q.GetZ(),q.GetW()};}return n;
 }
};
}
static std::once_flag initialized;static thread_local std::string error;
static void EnsureJoltInitialized(){std::call_once(initialized,[]{RegisterDefaultAllocator();Factory::sInstance=new Factory();RegisterTypes();});}
#define API extern "C" __declspec(dllexport)
API const char* slp_error(){return error.c_str();}
API void* slp_create(){try{EnsureJoltInitialized();return new starlight::PhysicsWorld();}catch(const std::exception& e){error=e.what();return nullptr;}}
// Test hook: the next n terrain tile cooks fail. Used by the physics ABI checks to prove a failed cook is recovered (audit EL-09).
API void slp_debug_fail_cooks(int n){starlight::PhysicsWorld::SetInjectedCookFailures(n<0?0:n);}
API void slp_debug_fail_updates(int n){starlight::PhysicsWorld::SetInjectedUpdateFailures(n<0?0:n);}
// Test hook: the next n body additions fail, as when the world is full (audit 03#2).
API void slp_debug_fail_adds(int n){starlight::PhysicsWorld::SetInjectedAddFailures(n<0?0:n);}
API void slp_destroy(void* p){delete static_cast<starlight::PhysicsWorld*>(p);}
API void slp_clear(void* p){static_cast<starlight::PhysicsWorld*>(p)->ClearProps();}
API int slp_terrain_mesh(void* p,const float* points,int count,const uint32_t* indices,int indexCount){try{static_cast<starlight::PhysicsWorld*>(p)->TerrainMesh(points,count,indices,indexCount);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_terrain(void* p,const float* heights){try{if(!heights)throw std::runtime_error("Missing terrain heights");static_cast<starlight::PhysicsWorld*>(p)->Terrain(heights);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_ready(void* p){try{return static_cast<starlight::PhysicsWorld*>(p)->Poll()?1:0;}catch(const std::exception& e){error=e.what();return -1;}}
API uint32_t slp_add(void* p,const float* points,int count,const uint32_t* indices,int indexCount,int dynamic,float density,float x,float y,float z,float qx,float qy,float qz,float qw){try{return static_cast<starlight::PhysicsWorld*>(p)->Add(points,count,indices,indexCount,dynamic!=0,density,Vec3(x,y,z),Quat(qx,qy,qz,qw));}catch(const std::exception& e){error=e.what();return 0xffffffff;}}
// World-independent prebuilt convex hulls. slp_hull_create may be called from any thread (it only runs Jolt's hull builder; the
// shape is reference counted with atomics); slp_add_hull must run on the thread that owns the world, like slp_add. The handle may
// be released on any thread, before or after the bodies made from it are destroyed. Returns null on failure (slp_error() is per thread).
struct HullHandle {ShapeRefC shape;starlight::PhysicsWorld::HullInfo info;};
API void* slp_hull_create(const float* points,int count,float density){try{if(!points||count<4||count>200000)throw std::runtime_error("Invalid collision mesh");EnsureJoltInitialized();auto* handle=new HullHandle();try{handle->shape=starlight::PhysicsWorld::BuildHull(points,count,density,&handle->info);}catch(...){delete handle;throw;}return handle;}catch(const std::exception& e){error=e.what();return nullptr;}}
API void slp_hull_release(void* hull){delete static_cast<HullHandle*>(hull);}
// out[0..4] = kind (0 exact, 1 exact after finer retry, 2 reduced to the 256-vertex limit, 3 bounding box, 4 rebuilt from the builder's own vertices), input points, hull points, measured deviation of the input from the hull, bounding-box diagonal.
API int slp_hull_info(void* hull,float* out){if(!hull||!out)return 0;const auto& info=static_cast<HullHandle*>(hull)->info;out[0]=float(info.kind);out[1]=float(info.inputPoints);out[2]=float(info.hullPoints);out[3]=info.deviation;out[4]=info.diagonal;return 1;}
// Writes the vertices of a hull (x, y, z each, in the space of the points it was built from) and returns how many; 0 for a box fallback.
API int slp_hull_points(void* hull,float* out,int capacity){
 if(!hull||!out)return 0;const Shape* shape=static_cast<HullHandle*>(hull)->shape.GetPtr();
 if(shape->GetSubType()!=EShapeSubType::ConvexHull)return 0;
 const auto* convex=static_cast<const ConvexHullShape*>(shape);const int n=int(convex->GetNumPoints());if(n>capacity)return 0;
 const Vec3 center=convex->GetCenterOfMass();
 for(int i=0;i<n;++i){const Vec3 p=convex->GetPoint(uint(i))+center;out[3*i]=p.GetX();out[3*i+1]=p.GetY();out[3*i+2]=p.GetZ();}
 return n;
}
// out[0..8] = hulls requested, exact, exact after finer retry, reduced to the vertex limit, of those beyond the accepted bound, replaced by a box, rejected (non-finite), worst reduced deviation relative to the hull diagonal, rebuilt from the builder's own vertices.
API void slp_hull_stats(double* out){auto& s=starlight::HullCounters();out[0]=double(s.requested.load());out[1]=double(s.exact.load());out[2]=double(s.finer.load());out[3]=double(s.reduced.load());out[4]=double(s.reducedOverBound.load());out[5]=double(s.boxed.load());out[6]=double(s.rejected.load());out[7]=double(s.worstRelative.load());out[8]=double(s.rebuilt.load());}
API int slp_hull_last_box(char* buffer,int capacity){auto& s=starlight::HullCounters();std::lock_guard<std::mutex> guard(s.lock);if(!buffer||capacity<=0)return 0;const int n=int(std::min<size_t>(s.lastBox.size(),size_t(capacity-1)));std::memcpy(buffer,s.lastBox.data(),size_t(n));buffer[n]=0;return n;}
API void slp_hull_reset_stats(){auto& s=starlight::HullCounters();s.requested=s.exact=s.finer=s.reduced=s.reducedOverBound=s.boxed=s.rejected=s.rebuilt=0;s.worstDeviationBits=0;s.worstRelative=0;std::lock_guard<std::mutex> guard(s.lock);s.lastBox.clear();}
// out[0..4] = cooked shapes held, bytes held, shared hits, misses, entries dropped for room.
API void slp_shape_cache_stats(double* out){auto& s=starlight::SharedShapeCache();std::lock_guard<std::mutex> guard(s.lock);out[0]=double(s.shapes.size());out[1]=double(s.bytes);out[2]=double(s.hits.load());out[3]=double(s.misses.load());out[4]=double(s.trimmed.load());}
API uint32_t slp_add_hull(void* p,void* hull,float x,float y,float z,float qx,float qy,float qz,float qw){try{if(!hull)throw std::runtime_error("Missing collision hull");return static_cast<starlight::PhysicsWorld*>(p)->AddShape(static_cast<HullHandle*>(hull)->shape,true,Vec3(x,y,z),Quat(qx,qy,qz,qw));}catch(const std::exception& e){error=e.what();return 0xffffffff;}}
API int slp_step(void* p,double dt,const starlight::Liquid* liquid,int count,starlight::BodyState* output,int capacity){try{return static_cast<starlight::PhysicsWorld*>(p)->Step(dt,liquid,count,output,capacity);}catch(const std::exception& e){error=e.what();return -1;}}
API int slp_present(void* p,starlight::BodyState* output,int capacity){try{return static_cast<starlight::PhysicsWorld*>(p)->Present(output,capacity);}catch(const std::exception& e){error=e.what();return -1;}}
API uint64_t slp_cooks(void* p){return static_cast<starlight::PhysicsWorld*>(p)->cooks;}
API uint64_t slp_ticks(void* p){return static_cast<starlight::PhysicsWorld*>(p)->ticks;}
API uint64_t slp_dropped_ticks(void* p){return static_cast<starlight::PhysicsWorld*>(p)->droppedTicks;}
API int slp_body_drive(void* p,uint32_t raw,float x,float y,float z,int vertical){try{if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))throw std::runtime_error("Invalid dynamic body drive");auto& w=*static_cast<starlight::PhysicsWorld*>(p);auto& api=w.world.GetBodyInterface();BodyID id(raw);auto velocity=api.GetLinearVelocity(id);api.SetLinearVelocity(id,Vec3(x,vertical?y:velocity.GetY(),z));api.ActivateBody(id);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_body_current(void* p,uint32_t raw,float x,float z){try{if(!std::isfinite(x)||!std::isfinite(z)||std::abs(x)>20||std::abs(z)>20)throw std::runtime_error("Invalid liquid current");auto& w=*static_cast<starlight::PhysicsWorld*>(p);if(x==0&&z==0)w.liquidCurrents.erase(raw);else w.liquidCurrents[raw]=Vec3(x,0,z);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_body_liquid(void* p,uint32_t raw,float height,float ratio,float dt){try{if(!std::isfinite(height)||!std::isfinite(ratio)||!std::isfinite(dt)||ratio<0||dt<=0||dt>.05f)throw std::runtime_error("Invalid liquid impulse");auto& w=*static_cast<starlight::PhysicsWorld*>(p);w.world.GetBodyInterface().ApplyBuoyancyImpulse(BodyID(raw),RVec3(0,height,0),Vec3::sAxisY(),ratio,.9f,.3f,w.liquidCurrents.count(raw)?w.liquidCurrents.at(raw):Vec3::sZero(),w.world.GetGravity(),dt);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_rope(void* p,uint32_t source,uint32_t target,const float* settings,int segments,float radius){try{static_cast<starlight::PhysicsWorld*>(p)->AddCable(source,target,settings,segments,radius);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_rope_path(void* p,uint32_t source,float* out,int capacity,int interpolate){try{return static_cast<starlight::PhysicsWorld*>(p)->CablePath(source,out,capacity,interpolate!=0);}catch(const std::exception& e){error=e.what();return -1;}}
API int slp_joint(void* p,uint32_t source,uint32_t target,int kind,const float* settings){try{static_cast<starlight::PhysicsWorld*>(p)->AddJoint(source,target,kind,settings);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_body_impulse(void* p,uint32_t raw,float x,float y,float z,float ix,float iy,float iz){try{for(float value:{x,y,z,ix,iy,iz})if(!std::isfinite(value))throw std::runtime_error("Invalid body impulse");auto& api=static_cast<starlight::PhysicsWorld*>(p)->world.GetBodyInterface();BodyID id(raw);if(api.GetMotionType(id)==EMotionType::Dynamic)api.AddImpulse(id,Vec3(ix,iy,iz),RVec3(x,y,z));return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_body_material(void* p,uint32_t raw,float friction,float restitution){try{if(!std::isfinite(friction)||!std::isfinite(restitution)||friction<0||friction>2||restitution<0||restitution>1)throw std::runtime_error("Invalid physical material");auto& api=static_cast<starlight::PhysicsWorld*>(p)->world.GetBodyInterface();api.SetFriction(BodyID(raw),friction);api.SetRestitution(BodyID(raw),restitution);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API void slp_body_velocity(void* p,uint32_t raw,float x,float y,float z,float ax,float ay,float az){try{for(float value:{x,y,z,ax,ay,az})if(!std::isfinite(value))throw std::runtime_error("Invalid body velocity");auto& w=*static_cast<starlight::PhysicsWorld*>(p);{BodyLockRead lock(w.world.GetBodyLockInterface(),BodyID(raw));if(!lock.Succeeded())throw std::runtime_error("Body no longer exists");}w.world.GetBodyInterface().SetLinearAndAngularVelocity(BodyID(raw),Vec3(x,y,z),Vec3(ax,ay,az));}catch(const std::exception& e){error=e.what();}}
API int slp_body_motion(void* p,uint32_t raw,float x,float y,float z,float* output){try{if(!p||!output||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))throw std::runtime_error("Invalid body motion sample");auto& w=*static_cast<starlight::PhysicsWorld*>(p);BodyLockRead lock(w.world.GetBodyLockInterface(),BodyID(raw));if(!lock.Succeeded())throw std::runtime_error("Body no longer exists");const auto& body=lock.GetBody();auto velocity=body.GetPointVelocity(RVec3(x,y,z));auto spin=body.GetAngularVelocity();output[0]=velocity.GetX();output[1]=velocity.GetY();output[2]=velocity.GetZ();output[3]=spin.GetX();output[4]=spin.GetY();output[5]=spin.GetZ();return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_body_pose(void* p,uint32_t raw,float* output){try{auto& w=*static_cast<starlight::PhysicsWorld*>(p);BodyLockRead lock(w.world.GetBodyLockInterface(),BodyID(raw));if(!lock.Succeeded())throw std::runtime_error("Body no longer exists");auto at=lock.GetBody().GetPosition();auto q=lock.GetBody().GetRotation();output[0]=(float)at.GetX();output[1]=(float)at.GetY();output[2]=(float)at.GetZ();output[3]=q.GetX();output[4]=q.GetY();output[5]=q.GetZ();output[6]=q.GetW();return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_character_start(void* p,float radius,float x,float y,float z){try{static_cast<starlight::PhysicsWorld*>(p)->StartCharacter(radius,Vec3(x,y,z));return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_character_traverse(void* p,float x,float y,float z,float dx,float dy,float dz){try{return static_cast<starlight::PhysicsWorld*>(p)->CharacterTraverse(Vec3(x,y,z),Vec3(dx,dy,dz))?1:0;}catch(const std::exception& e){error=e.what();return -1;}}
API int slp_character_height(void* p,float height){try{return static_cast<starlight::PhysicsWorld*>(p)->CharacterHeight(height)?1:0;}catch(const std::exception& e){error=e.what();return -1;}}
API int slp_character_current(void* p,float x,float z){try{if(!std::isfinite(x)||!std::isfinite(z)||std::abs(x)>20||std::abs(z)>20)throw std::runtime_error("Invalid character current");static_cast<starlight::PhysicsWorld*>(p)->characterCurrent=Vec3(x,0,z);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_character_liquid(void* p,float height,float density){try{if(!std::isfinite(height)||!std::isfinite(density)||density<0||density>20000)throw std::runtime_error("Invalid character liquid");auto& w=*static_cast<starlight::PhysicsWorld*>(p);w.characterLiquidHeight=height;w.characterLiquidDensity=density;return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_character_step(void* p,float dt,float x,float y,float z,float vx,float vz,int jump,float* output){try{static_cast<starlight::PhysicsWorld*>(p)->CharacterStep(dt,Vec3(x,y,z),Vec3(vx,0,vz),jump!=0,output);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_character_motion(void* p,float dt,float x,float y,float z,float vx,float vy,float vz,float* output){try{static_cast<starlight::PhysicsWorld*>(p)->CharacterStep(dt,Vec3(x,y,z),Vec3(vx,vy,vz),false,output,true);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API void slp_character_remove(void* p,uint32_t raw){try{auto& w=*static_cast<starlight::PhysicsWorld*>(p);BodyID id(raw);if(id.IsInvalid()){error="Invalid body id";return;}bool live;{BodyLockRead lock(w.world.GetBodyLockInterface(),id);live=lock.Succeeded();}w.liquidCurrents.erase(raw);w.RemoveCables(id);w.RemoveJoints(id);if(live){w.world.GetBodyInterface().RemoveBody(id);w.world.GetBodyInterface().DestroyBody(id);}else error="Body no longer exists";w.allProps.erase(std::remove(w.allProps.begin(),w.allProps.end(),id),w.allProps.end());w.bodies.erase(std::remove(w.bodies.begin(),w.bodies.end(),id),w.bodies.end());w.previousBodies.erase(std::remove_if(w.previousBodies.begin(),w.previousBodies.end(),[raw](const starlight::BodyState& b){return b.id==raw;}),w.previousBodies.end());{std::lock_guard lock(w.contacts.mutex);w.contacts.touching.erase(raw);}}catch(const std::exception& e){error=e.what();}}
API int slp_kinematic_pose(void* p,uint32_t raw,float x,float y,float z,float qx,float qy,float qz,float qw,float dt){try{for(float value:{x,y,z,qx,qy,qz,qw,dt})if(!std::isfinite(value))throw std::runtime_error("Nonfinite kinematic pose");if(dt<=0||dt>.1f)throw std::runtime_error("Invalid kinematic interval");Quat rotation(qx,qy,qz,qw);if(rotation.LengthSq()<1e-10f)throw std::runtime_error("Invalid kinematic rotation");auto& api=static_cast<starlight::PhysicsWorld*>(p)->world.GetBodyInterface();BodyID id(raw);api.SetMotionType(id,EMotionType::Kinematic,EActivation::Activate);api.MoveKinematic(id,RVec3(x,y,z),rotation.Normalized(),dt);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API void slp_character_platform(void* p,uint32_t raw,float x,float y,float z,float dt){try{for(float value:{x,y,z,dt})if(!std::isfinite(value))throw std::runtime_error("Nonfinite platform pose");if(dt<=0||dt>.1f)throw std::runtime_error("Invalid platform interval");auto& w=*static_cast<starlight::PhysicsWorld*>(p);BodyID id(raw);{BodyLockRead lock(w.world.GetBodyLockInterface(),id);if(!lock.Succeeded())throw std::runtime_error("Body no longer exists");}auto& api=w.world.GetBodyInterface();api.SetMotionType(id,EMotionType::Kinematic,EActivation::Activate);api.MoveKinematic(id,RVec3(x,y,z),Quat::sIdentity(),dt);}catch(const std::exception& e){error=e.what();}}
API void slp_character_actor(void* p,uint32_t raw,float x,float y,float z,float yaw,float roll,float dt){try{for(float value:{x,y,z,yaw,roll,dt})if(!std::isfinite(value))throw std::runtime_error("Nonfinite actor pose");if(dt<=0||dt>.1f)throw std::runtime_error("Invalid actor interval");auto& w=*static_cast<starlight::PhysicsWorld*>(p);BodyID id(raw);{BodyLockRead lock(w.world.GetBodyLockInterface(),id);if(!lock.Succeeded())throw std::runtime_error("Body no longer exists");}auto& api=w.world.GetBodyInterface();api.SetMotionType(id,EMotionType::Kinematic,EActivation::Activate);api.MoveKinematic(id,RVec3(x,y,z),Quat::sRotation(Vec3(-std::sin(roll),std::cos(roll),0),yaw),dt);}catch(const std::exception& e){error=e.what();}}

