// Input-rejection checks for the physics C ABI (audit EL-07). Each hostile call must return an error and leave the world usable; a valid call afterwards proves it.
extern "C" {
__declspec(dllimport) void *slp_create();
__declspec(dllimport) void slp_destroy(void *);
__declspec(dllimport) const char *slp_error();
__declspec(dllimport) uint32_t slp_add(void *, const float *, int, const uint32_t *, int, int, float, float, float, float, float, float, float, float);
__declspec(dllimport) void slp_body_velocity(void *, uint32_t, float, float, float, float, float, float);
__declspec(dllimport) void slp_character_remove(void *, uint32_t);
__declspec(dllimport) void slp_character_platform(void *, uint32_t, float, float, float, float);
__declspec(dllimport) void slp_character_actor(void *, uint32_t, float, float, float, float, float, float);
__declspec(dllimport) int slp_terrain(void *, const float *);
__declspec(dllimport) int slp_ready(void *);
__declspec(dllimport) void slp_debug_fail_cooks(int);
__declspec(dllimport) int slp_rope(void *, uint32_t, uint32_t, const float *, int, float);
__declspec(dllimport) int slp_character_start(void *, float, float, float, float);
__declspec(dllimport) int slp_character_step(void *, float, float, float, float, float, float, int, float *);
__declspec(dllimport) int slp_terrain_mesh(void *, const float *, int, const uint32_t *, int);
__declspec(dllimport) int slp_rope_path(void *, uint32_t, float *, int, int);
__declspec(dllimport) void *slp_hull_create(const float *, int, float);
}
static void physics_abi_checks(const auto& require){
    const uint32_t refused=0xffffffffu;
    const float cube[24]={-.5f,-.5f,-.5f, .5f,-.5f,-.5f, .5f,.5f,-.5f, -.5f,.5f,-.5f, -.5f,-.5f,.5f, .5f,-.5f,.5f, .5f,.5f,.5f, -.5f,.5f,.5f};
    const uint32_t faces[36]={0,1,2,0,2,3, 4,6,5,4,7,6, 0,4,5,0,5,1, 1,5,6,1,6,2, 2,6,7,2,7,3, 3,7,4,3,4,0};
    const float nan=std::numeric_limits<float>::quiet_NaN();
    void* world=slp_create();
    require(world!=nullptr,"EL-07: A physics world is created for the input checks");
    if(!world)return;
    auto add=[&](bool dynamic,float density,float x,float qx,float qw,int indexCount=36,const float* points=nullptr){return slp_add(world,points?points:cube,8,faces,indexCount,dynamic?1:0,density,x,1,0,qx,0,0,qw);};
    uint32_t good=add(true,1,0,0,1);
    require(good!=refused,"EL-07: A valid dynamic body is still accepted");
    require(add(true,1,nan,0,1)==refused&&std::string(slp_error()).find("pose")!=std::string::npos,"EL-07: A body at a non-finite position is refused with a pose error");
    require(add(true,1,0,0,0)==refused&&std::string(slp_error()).find("rotation")!=std::string::npos,"EL-07: A zero quaternion is refused instead of normalising to NaN");
    require(add(true,1,0,nan,1)==refused,"EL-07: A non-finite rotation is refused");
    require(add(true,0,0,0,1)==refused&&add(true,-1,0,0,1)==refused&&add(true,nan,0,0,1)==refused,"EL-07: Zero, negative and non-finite density are refused for a dynamic body");
    require(add(false,nan,0,0,1)==refused,"EL-07: A non-finite density is refused even for a static body");
    require(add(true,1,0,0,1,6000003)==refused,"EL-07: An index count above the 6,000,000 cap is refused before any index is read");
    require(slp_add(world,nullptr,8,faces,36,1,1,0,1,0,0,0,0,1)==refused&&slp_add(world,cube,8,nullptr,36,1,1,0,1,0,0,0,0,1)==refused,"EL-07: Null point or index arrays are refused");
    float damaged[24];std::memcpy(damaged,cube,sizeof(cube));damaged[3]=nan;
    require(add(false,1,0,0,1,36,damaged)==refused&&std::string(slp_error()).find("vertex")!=std::string::npos,"EL-07: A static mesh with a non-finite vertex is refused");
    require(slp_terrain(world,nullptr)==0&&std::string(slp_error()).find("terrain")!=std::string::npos,"EL-07: A null terrain height array is refused");
    uint32_t after=add(true,1,3,0,1);
    require(after!=refused&&after!=good,"EL-07: The world still accepts a valid body after every refusal");
    slp_body_velocity(world,good,nan,0,0,0,0,0);
    require(std::string(slp_error()).find("velocity")!=std::string::npos,"EL-07: A non-finite body velocity is refused with an error instead of reaching Jolt");
    slp_body_velocity(world,0x7ffffff0u,0,0,0,0,0,0);
    require(std::string(slp_error()).find("no longer exists")!=std::string::npos,"EL-07: A velocity for a body that does not exist is refused");
    slp_character_platform(world,good,0,1,0,0);
    require(std::string(slp_error()).find("interval")!=std::string::npos,"EL-07: A platform move with dt of zero is refused");
    slp_character_platform(world,good,nan,1,0,.016f);
    require(std::string(slp_error()).find("Nonfinite platform pose")!=std::string::npos,"EL-07: A non-finite platform pose is refused");
    slp_character_actor(world,good,0,1,0,nan,0,.016f);
    require(std::string(slp_error()).find("Nonfinite actor pose")!=std::string::npos,"EL-07: A non-finite actor yaw is refused");
    slp_character_remove(world,after);
    slp_character_remove(world,after);
    require(std::string(slp_error()).find("no longer exists")!=std::string::npos,"EL-07: Removing the same body twice is refused the second time instead of reaching Jolt");
    require(add(true,1,5,0,1)!=refused,"EL-07: The world still accepts a valid body after a double remove");
    // EL-08: a rope that fails part-way must not leave half-built links in the world.
    std::vector<uint32_t> fillers;
    for(int i=0;i<9000;++i){uint32_t id=slp_add(world,cube,8,faces,36,0,1,float(i)*.01f,-5,0,0,0,0,1);if(id==refused)break;fillers.push_back(id);}
    require(fillers.size()>1000&&fillers.size()<9000,"EL-08: The test world is filled to its body capacity");
    for(int i=0;i<4;++i){slp_character_remove(world,fillers.back());fillers.pop_back();}
    uint32_t anchor=add(true,1,0,0,1);
    require(anchor!=refused,"EL-08: One slot is left for the rope's source body");
    const float settings[10]={0,1,0, 2,1,0, 0,0,0, 3};
    require(slp_rope(world,anchor,UINT32_MAX,settings,8,.05f)==0&&std::string(slp_error()).find("capacity")!=std::string::npos,"EL-08: A rope that runs out of body slots part-way is refused with a capacity error");
    int regained=0;for(int i=0;i<3;++i)if(slp_add(world,cube,8,faces,36,0,1,float(i)*.01f,-6,0,0,0,0,1)!=refused)++regained;
    require(regained==3,"EL-08: The slots used by the failed rope's links are given back");
    // EL-09: a terrain tile whose cook failed must be cooked again by the next terrain update.
    void* terrainWorld=slp_create();
    require(terrainWorld!=nullptr,"EL-09: A fresh physics world is created for the terrain checks");
    if(!terrainWorld){slp_destroy(world);return;}
    std::vector<float> heights(97*97,0.f);
    auto settle=[&](int polls){int state=0;for(int i=0;i<polls&&state==0;++i){state=slp_ready(terrainWorld);if(state==0)std::this_thread::sleep_for(std::chrono::milliseconds(2));}return state;};
    slp_debug_fail_cooks(36);
    require(slp_terrain(terrainWorld,heights.data())==1,"EL-09: A terrain update is accepted while every tile cook is made to fail");
    int failures=0;for(int i=0;i<80;++i){int state=settle(200);if(state==-1)++failures;else break;}
    require(failures>=1&&std::string(slp_error()).find("cook failed")!=std::string::npos,"EL-09: The failed cooks are reported as errors");
    require(slp_terrain(terrainWorld,heights.data())==1,"EL-09: The same terrain update is accepted again after the failures");
    int finalState=0;for(int i=0;i<10&&finalState!=1;++i)finalState=settle(2000);
    require(finalState==1,"EL-09: Every tile is cooked again and the terrain becomes ready after the failed cooks");
    // TERR CHAR-1: a non-finite character step is refused and cannot poison the controller for later valid steps.
    require(slp_character_start(terrainWorld,.3f,0,0,0)==1,"TERR CHAR-1: A character controller starts on the cooked terrain");
    float stepped[4]{};const float infinity=std::numeric_limits<float>::infinity();
    require(slp_character_step(terrainWorld,1.f/60,0,0,0,nan,0,0,stepped)==0&&slp_character_step(terrainWorld,1.f/60,nan,0,0,0,0,0,stepped)==0&&slp_character_step(terrainWorld,1.f/60,0,infinity,0,0,0,0,stepped)==0&&std::string(slp_error()).find("character step")!=std::string::npos,"TERR CHAR-1: A character step with non-finite feet or input is refused");
    require(slp_character_step(terrainWorld,1.f/60,0,0,0,0,0,0,stepped)==1&&std::isfinite(stepped[0]+stepped[1]+stepped[2]),"TERR CHAR-1: A finite step still works after a refused one");
    // audit2 F5: a character cannot start from a non-finite position or radius; a NaN start used to leave the controller at NaN for good.
    {
     float afterRefusal[4]{};
     require(slp_character_start(terrainWorld,.3f,nan,0,0)==0&&std::string(slp_error()).find("character start")!=std::string::npos,"audit2 F5: A character cannot start at a non-finite position");
     require(slp_character_start(terrainWorld,nan,0,0,0)==0&&slp_character_start(terrainWorld,0,0,0,0)==0&&slp_character_start(terrainWorld,3.f,0,0,0)==0,"audit2 F5: A character radius that is not finite or is outside 0.05 to 2 metres is refused");
     require(slp_character_step(terrainWorld,1.f/60,0,0,0,0,0,0,afterRefusal)==1&&std::isfinite(afterRefusal[0]+afterRefusal[1]+afterRefusal[2]),"audit2 F5: A refused start leaves the existing character stepping normally");
    }
    // audit2 F6: a dynamic body or prebuilt hull cannot be made from a non-finite vertex; the hull builder used to accept it and the body got a NaN shape.
    {
     void* hulls=slp_create();
     float holed[24];std::memcpy(holed,cube,sizeof(cube));holed[3]=nan;
     require(hulls&&slp_add(hulls,holed,8,faces,36,1,1,0,1,0,0,0,0,1)==refused&&std::string(slp_error()).find("vertex")!=std::string::npos,"audit2 F6: A dynamic body with a non-finite vertex is refused");
     require(slp_hull_create(holed,8,1)==nullptr&&std::string(slp_error()).find("vertex")!=std::string::npos,"audit2 F6: A prebuilt hull with a non-finite vertex is refused");
     slp_destroy(hulls);
    }
    // audit2 F2: removing the "no body" id must not remove every rope anchored to the world (an invalid id compares equal to the world anchor).
    {
     void* anchors=slp_create();
     require(anchors!=nullptr,"audit2 F2: A physics world is created for the anchor check");
     if(anchors){
      uint32_t anchored=slp_add(anchors,cube,8,faces,36,1,1,0,1,0,0,0,0,1);
      const float ropeSettings[10]={0,1,0, 2,1,0, 0,0,0, 3};
      require(anchored!=refused&&slp_rope(anchors,anchored,UINT32_MAX,ropeSettings,8,.05f)==1,"audit2 F2: A rope anchored to the world is built for the removal check");
      slp_character_remove(anchors,UINT32_MAX);
      float path[65*3]{};
      require(slp_rope_path(anchors,anchored,path,65,0)==9,"audit2 F2: Removing the invalid body id leaves the world-anchored rope in place");
      slp_destroy(anchors);
     }
    }
    // audit2 F3: a terrain update with a non-finite height is refused before the volumetric terrain it would replace is removed.
    {
     void* volume=slp_create();
     const float quad[12]={-5,0,-5, 5,0,-5, 5,0,5, -5,0,5};const uint32_t tris[6]={0,1,2,0,2,3};
     require(volume&&slp_terrain_mesh(volume,quad,4,tris,6)==1&&slp_ready(volume)==1,"audit2 F3: A volumetric terrain is installed for the refusal check");
     std::vector<float> damaged(97*97,0.f);damaged.back()=nan;
     require(slp_terrain(volume,damaged.data())==0&&std::string(slp_error()).find("terrain")!=std::string::npos,"audit2 F3: A terrain update with a non-finite height is refused");
     require(slp_ready(volume)==1,"audit2 F3: A refused terrain update leaves the volumetric terrain in place and the world ready");
     slp_destroy(volume);
    }
    // audit2 F1: a terrain tile cooked while the world has no free body slot keeps its shape and gets a body once a slot is freed.
    {
     void* full=slp_create();
     std::vector<uint32_t> filler;
     for(int i=0;i<9000;++i){uint32_t id=slp_add(full,cube,8,faces,36,0,1,float(i)*.01f,-5,0,0,0,0,1);if(id==refused)break;filler.push_back(id);}
     const std::vector<float> flatHeights(97*97,0.f);
     require(full&&filler.size()>1000&&filler.size()<9000&&slp_terrain(full,flatHeights.data())==1,"audit2 F1: The world is filled to its body capacity before the terrain cooks");
     int failures=0;for(int i=0;i<5000&&failures==0;++i){if(slp_ready(full)==-1)++failures;else std::this_thread::sleep_for(std::chrono::milliseconds(2));}
     require(failures>=1&&std::string(slp_error()).find("capacity")!=std::string::npos,"audit2 F1: A terrain tile with no free body slot is reported as a capacity error");
     for(int i=0;i<40;++i){slp_character_remove(full,filler.back());filler.pop_back();}
     int state=0;for(int i=0;i<5000&&state!=1;++i){state=slp_ready(full);if(state==0)std::this_thread::sleep_for(std::chrono::milliseconds(2));}
     require(state==1,"audit2 F1: The terrain becomes ready once a body slot is freed, instead of staying not ready for good");
     slp_destroy(full);
    }
    slp_destroy(terrainWorld);
    slp_destroy(world);
}
