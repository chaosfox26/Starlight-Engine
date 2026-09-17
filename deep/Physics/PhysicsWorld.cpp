#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <future>
#include <array>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_set>
#include <cmath>
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
 struct Tile {BodyID body;std::array<float,289> heights{},requested{};std::future<ShapeRefC> pending;bool valid=false;};
 std::array<Tile,36> tiles;std::vector<BodyID> bodies,allProps;double accumulator=0;uint64_t ticks=0,cooks=0;
 PhysicsWorld(){world.Init(8192,0,32768,32768,broad,filter,pairs);world.SetContactListener(&contacts);}
 ~PhysicsWorld(){for(auto& tile:tiles)if(tile.pending.valid())tile.pending.wait();}
 void ClearProps(){auto& api=world.GetBodyInterface();for(auto id:allProps){api.RemoveBody(id);api.DestroyBody(id);}bodies.clear();allProps.clear();contacts.touching.clear();accumulator=0;ticks=0;}
 static ShapeRefC Cook(const std::array<float,289>& h,int tx,int tz){
  TriangleList triangles;triangles.reserve(512);
  auto point=[&](int x,int z){return Float3(-12+(tx*16+x)*.25f,h[z*17+x],-12+(tz*16+z)*.25f);};
  for(int z=0;z<16;z++)for(int x=0;x<16;x++){auto a=point(x,z),b=point(x+1,z),c=point(x,z+1),d=point(x+1,z+1);triangles.emplace_back(a,c,b);triangles.emplace_back(b,c,d);}
  MeshShapeSettings settings(triangles);auto result=settings.Create();if(result.HasError())throw std::runtime_error(result.GetError().c_str());return result.Get();
 }
 void Terrain(const float* heights){
  Poll();for(int tz=0;tz<6;tz++)for(int tx=0;tx<6;tx++){auto& t=tiles[tz*6+tx];std::array<float,289> data;for(int z=0;z<17;z++)for(int x=0;x<17;x++){float h=heights[(tz*16+z)*97+tx*16+x];if(!std::isfinite(h))throw std::runtime_error("Invalid terrain height");data[z*17+x]=h;}t.requested=data;if(t.pending.valid()||(t.valid&&t.heights==data))continue;t.heights=data;t.valid=true;t.pending=std::async(std::launch::async,[data,tx,tz]{return Cook(data,tx,tz);});cooks++;}
 }
 bool Poll(){bool ready=true;auto& api=world.GetBodyInterface();for(auto& t:tiles){if(t.pending.valid()){if(t.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready){auto shape=t.pending.get();if(t.body.IsInvalid()){BodyCreationSettings settings(shape,RVec3::sZero(),Quat::sIdentity(),EMotionType::Static,0);settings.mFriction=.72f;t.body=api.CreateAndAddBody(settings,EActivation::DontActivate);if(t.body.IsInvalid())throw std::runtime_error("Terrain body capacity reached");}else api.SetShape(t.body,shape,true,EActivation::Activate);}else ready=false;}if(t.valid&&!t.pending.valid()&&t.requested!=t.heights){t.heights=t.requested;auto data=t.heights;int index=(int)(&t-tiles.data());t.pending=std::async(std::launch::async,[data,index]{return Cook(data,index%6,index/6);});cooks++;ready=false;}if(t.body.IsInvalid())ready=false;}return ready;}
 uint32_t Add(const float* points,int count,const uint32_t* indices,int indexCount,bool dynamic,float density,Vec3 pos,Quat rotation){
  if(count<4||count>200000||indexCount<3||indexCount%3)throw std::runtime_error("Invalid collision mesh");ShapeRefC shape;
  if(dynamic){Array<Vec3> vertices;vertices.reserve(count);for(int i=0;i<count;i++)vertices.emplace_back(points[i*3],points[i*3+1],points[i*3+2]);ConvexHullShapeSettings settings(vertices);settings.mDensity=density;auto result=settings.Create();if(result.HasError())throw std::runtime_error(result.GetError().c_str());shape=result.Get();}
  else {TriangleList triangles;for(int i=0;i<indexCount;i+=3){Float3 p[3];for(int j=0;j<3;j++){auto k=indices[i+j];if(k>=(uint32_t)count)throw std::runtime_error("Invalid collision index");p[j]=Float3(points[k*3],points[k*3+1],points[k*3+2]);}triangles.emplace_back(p[0],p[1],p[2]);}MeshShapeSettings settings(triangles);auto result=settings.Create();if(result.HasError())throw std::runtime_error(result.GetError().c_str());shape=result.Get();}
  BodyCreationSettings settings(shape,RVec3(pos),rotation.Normalized(),dynamic?EMotionType::Dynamic:EMotionType::Static,dynamic?1:0);settings.mFriction=.72f;settings.mRestitution=.03f;settings.mAngularDamping=.18f;settings.mLinearDamping=.03f;settings.mMotionQuality=dynamic?EMotionQuality::LinearCast:EMotionQuality::Discrete;
  BodyID id=world.GetBodyInterface().CreateAndAddBody(settings,dynamic?EActivation::Activate:EActivation::DontActivate);if(id.IsInvalid())throw std::runtime_error("Body capacity reached");allProps.push_back(id);if(dynamic)bodies.push_back(id);return id.GetIndexAndSequenceNumber();
 }
 int Step(double elapsed,const Liquid* liquid,int liquidCount,BodyState* output,int capacity){
  if(!std::isfinite(elapsed)||elapsed<0||elapsed>1)throw std::runtime_error("Invalid physics elapsed time");if(!Poll())return 0;accumulator+=elapsed;auto& api=world.GetBodyInterface();int steps=0;
  while(accumulator>=1.0/60&&steps++<12){
   for(int i=0;i<liquidCount;i++){auto f=liquid[i];BodyID id(f.id);if(f.ratio>0)api.ApplyBuoyancyImpulse(id,RVec3(0,f.height,0),Vec3::sAxisY(),f.ratio,.9f,.3f,Vec3::sZero(),world.GetGravity(),1.f/60);}
   // Coulomb-style rolling resistance acts only on bodies with recent contacts.
   // Damping above remains a separate, small aerodynamic/numerical loss.
   for(auto raw:contacts.touching){BodyID id(raw);float torque=0;Vec3 omega=api.GetAngularVelocity(id);float speed=omega.Length();if(speed<.001f)continue;{BodyLockRead lock(world.GetBodyLockInterface(),id);if(lock.Succeeded()){auto& b=lock.GetBody();float inv=b.GetMotionProperties()->GetInverseMass();float radius=b.GetShape()->GetLocalBounds().GetExtent().Length();if(inv>0)torque=.012f/inv*9.81f*radius*std::min(1.f,speed);}}if(torque>0)api.AddTorque(id,-omega/speed*torque);}
   contacts.touching.clear();
   auto error=world.Update(1.f/60,1,&temp,&jobs);if(error!=EPhysicsUpdateError::None)throw std::runtime_error("Jolt capacity exceeded during step");accumulator-=1.0/60;ticks++;
  }
  int n=0;for(auto id:bodies){if(n>=capacity)break;RVec3 p;Quat q;api.GetPositionAndRotation(id,p,q);output[n++]={id.GetIndexAndSequenceNumber(),(float)p.GetX(),(float)p.GetY(),(float)p.GetZ(),q.GetX(),q.GetY(),q.GetZ(),q.GetW()};}return n;
 }
};
}
static std::once_flag initialized;static thread_local std::string error;
#define API extern "C" __declspec(dllexport)
API const char* slp_error(){return error.c_str();}
API void* slp_create(){try{std::call_once(initialized,[]{RegisterDefaultAllocator();Factory::sInstance=new Factory();RegisterTypes();});return new starlight::PhysicsWorld();}catch(const std::exception& e){error=e.what();return nullptr;}}
API void slp_destroy(void* p){delete static_cast<starlight::PhysicsWorld*>(p);}
API void slp_clear(void* p){static_cast<starlight::PhysicsWorld*>(p)->ClearProps();}
API int slp_terrain(void* p,const float* heights){try{static_cast<starlight::PhysicsWorld*>(p)->Terrain(heights);return 1;}catch(const std::exception& e){error=e.what();return 0;}}
API int slp_ready(void* p){try{return static_cast<starlight::PhysicsWorld*>(p)->Poll()?1:0;}catch(const std::exception& e){error=e.what();return -1;}}
API uint32_t slp_add(void* p,const float* points,int count,const uint32_t* indices,int indexCount,int dynamic,float density,float x,float y,float z,float qx,float qy,float qz,float qw){try{return static_cast<starlight::PhysicsWorld*>(p)->Add(points,count,indices,indexCount,dynamic!=0,density,Vec3(x,y,z),Quat(qx,qy,qz,qw));}catch(const std::exception& e){error=e.what();return 0xffffffff;}}
API int slp_step(void* p,double dt,const starlight::Liquid* liquid,int count,starlight::BodyState* output,int capacity){try{return static_cast<starlight::PhysicsWorld*>(p)->Step(dt,liquid,count,output,capacity);}catch(const std::exception& e){error=e.what();return -1;}}
API uint64_t slp_cooks(void* p){return static_cast<starlight::PhysicsWorld*>(p)->cooks;}
API uint64_t slp_ticks(void* p){return static_cast<starlight::PhysicsWorld*>(p)->ticks;}
